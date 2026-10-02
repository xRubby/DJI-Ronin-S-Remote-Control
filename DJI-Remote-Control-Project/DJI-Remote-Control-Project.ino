#include <WiFi.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Ronin_SBUS.h>
#include "LANC_CAM_CONTROL.h"

// ================= WIFI =================
const char* AP_SSID = "RoninControl_Setup";
IPAddress apIP(192, 168, 4, 1);

#define N_TENTATIVI 50

WebServer webServer(80);
WebSocketsServer webSocket(81);

// ================= WIFI CONFIG =================
struct WifiConfig {
  char wifi_ssid[32];
  char wifi_password[64];
  bool use_static_ip;
  IPAddress static_ip;
  IPAddress gateway;
  IPAddress subnet;
  uint8_t config_set;
};

WifiConfig wificonfig;

// ================= CONTROLLER =================
struct ControllerValues {
  int16_t x;
  int16_t y;
  int16_t rx;
  int16_t ry;
  int16_t a;
  int16_t b;
};

ControllerValues controllerValues;

// ================= PACCHETTI WEBSOCKET =================

uint32_t lastPacketTime = 0;
const uint32_t CONTROL_TIMEOUT = 100; // ms

// ================= RONIN =================
Ronin_SBUS ronin;
#define SBUS_PIN 14

const int sbusMID = 1024;
const int sbusMIN = 352;
const int sbusMAX = 1696;
const int sbusWAIT = 10;

bool lowspeed = false;
bool prevAState = false;

// ================= LANC =================
#define COMMAND_PIN 12
#define LANC_PIN 13

LANC_CAM_CONTROL cameraControl(COMMAND_PIN, LANC_PIN);

volatile int16_t zoomSpeed = 0;
bool prevBState = false;

// ================= TASK HANDLES =================
TaskHandle_t roninTaskHandle;
TaskHandle_t lancTaskHandle;

// Function to load wifi config from the struct
void loadWifiConfig() {
  Serial.println("Caricando...");
    EEPROM.get(0, wificonfig);
    if (wificonfig.config_set != 1) {
        wificonfig.config_set = 0;
        wificonfig.use_static_ip = false;
        strcpy(wificonfig.wifi_ssid, ""); // Vuoto per configurazione
        strcpy(wificonfig.wifi_password, ""); // Vuoto per configurazione
        wificonfig.static_ip = IPAddress(192, 168, 1, 200);
        wificonfig.gateway = IPAddress(192, 168, 1, 1);
        wificonfig.subnet = IPAddress(255, 255, 255, 0);
        Serial.println("Caricato da EEPROM");
    }
}

void saveWifiConfig() {
    Serial.println("Salvo in EEPROM...");
    EEPROM.put(0, wificonfig);
    EEPROM.commit();
    Serial.println("Salvato in EEPROM...");
}

void handleReset() {
    memset(&wificonfig, 0, sizeof(wificonfig));
    saveWifiConfig();
    Serial.println("Impostazioni Wi-Fi resettate. Sto riavviando...");
    delay(1000);
    ESP.restart();
}

// Function to start the ESP in setup mode --> generate open wifi ("RoninControl_Setup") to set up network settings
void startSetupMode() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID);
}

// Function to start the ESP in normal mode --> if it cannot connect within 25 attemps, it will restart in setup mode
void startNormalMode() {
    WiFi.mode(WIFI_STA);
    if (wificonfig.use_static_ip) {
        WiFi.config(wificonfig.static_ip, wificonfig.gateway, wificonfig.subnet);
    }
    WiFi.begin(wificonfig.wifi_ssid, wificonfig.wifi_password);
    Serial.println("SSID:");
    Serial.println(wificonfig.wifi_ssid);
    Serial.println("Password:");
    Serial.println(wificonfig.wifi_password);
    Serial.println("Connettendo al Wi-Fi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < N_TENTATIVI) {
        delay(1000);
        Serial.println("Tentativo di connessione #" + String(attempts));
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        configTime(3600, 0, "pool.ntp.org", "time.nist.gov");

        Serial.println("Connected to " + String(wificonfig.wifi_ssid));
        Serial.println("IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("Connection failed, starting setup mode");
        handleReset();
        startSetupMode();
    }
}

String scanWiFiHTML() {
  String options = "";
  int n = WiFi.scanNetworks();

  if (n == 0) {
    options = "<option>Nessuna rete trovata</option>";
  } else {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      options += "<option value=\"" + ssid + "\">" + ssid + "</option>";
    }
  }
  return options;
}

// Status page handling
void handleWiFiInfo() {
    String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Stato connessione</title>
<style>
  body { font-family: Arial, sans-serif; text-align: center; padding: 20px; background-color: #f4f4f4; }
  .container { max-width: 400px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
  h1 { color: #333; }
  p { font-size: 18px; }
  .button { display: block; width: 100%; padding: 10px; margin: 10px 0; border: none; border-radius: 5px; font-size: 18px; text-decoration: none; cursor: pointer; text-align: center; }
  .btn-log { background-color: #007bff; color: white; }
  .btn-home {background-color: #28a745; color: white; }
  .btn-config { background-color: #007bff; color: white; }
  .btn-reset { background-color: #dc3545; color: white; }
</style>
</head>
<body>
<div class='container'>
  <button class='button btn-home' onclick="location.href='/'">Home</button>
  <h1>Stato Connessione</h1>
  <p><strong>SSID:</strong> )rawliteral" + String(WiFi.SSID()) + R"rawliteral(</p>
  <p><strong>IP:</strong> )rawliteral" + WiFi.localIP().toString() + R"rawliteral(</p>
  <button class='button btn-config' onclick='configRedirect()'>Impostazioni WiFi</button>
  <button class='button btn-reset' onclick='confirmReset()'>Reset impostazioni WiFi</button>
</div>
<script>
  function confirmReset() {
    if (confirm('WiFi configuration will be reset and the device will reboot in setup mode. Continue?')) {
      setTimeout(() => { window.location.href = '/reset'; }, 500);
      setTimeout(() => { window.close(); }, 1000);
    }
  }
  function configRedirect() {
     window.location.href = '/wificonfig';
  }
</script>
</body>
</html>
)rawliteral";

    webServer.send(200, "text/html", page);
}

void handleWifiConfig() {
  String html;

  html += "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "  <title>WiFi Configuration</title>";
  html += "  <style>";
  html += "    body { font-family: Arial, sans-serif; max-width: 600px; margin: auto; padding: 20px; }";
  html += "    input[type=text], input[type=password], select { width: 100%; padding: 8px; margin: 5px 0; }";
  html += "    input[type=submit] { padding: 10px 20px; }";
  html += "  </style>";
  html += "</head>";

  html += "<body>";
  html += "  <h1>WiFi Configuration</h1>";
  html += "  <form id='wifiForm' action='/configure' method='POST'>";

  html += "    <label>SSID:</label><br>";
  html += "    <select name='ssid'>";
  html += scanWiFiHTML();
  html += "    </select><br><br>";

  html += "    <label>Password:</label><br>";
  html += "    <input type='password' name='password' required><br>";

  html += "    <label>Use static IP:</label><br>";
  html += "    <input type='checkbox' name='static_ip' id='static_ip'><br>";

  html += "    <div id='static_ip_fields' style='display:none; margin-top:10px;'>";
  html += "      <label>IP:</label><br>";
  html += "      <input type='text' name='ip' placeholder='192.168.1.100'><br>";
  html += "      <label>Gateway:</label><br>";
  html += "      <input type='text' name='gateway' placeholder='192.168.1.1'><br>";
  html += "      <label>Subnet:</label><br>";
  html += "      <input type='text' name='subnet' placeholder='255.255.255.0'><br>";
  html += "    </div><br>";

  html += "    <input type='submit' value='Save'>";
  html += "  </form>";
  html += "  <button onclick='configManualRedirect()'>Impostazioni WiFi Manuali</button>";

  html += "  <script>";
  html += "    document.getElementById('static_ip').addEventListener('change', function() {";
  html += "      document.getElementById('static_ip_fields').style.display = this.checked ? 'block' : 'none';";
  html += "    });";
  html += "    document.getElementById('wifiForm').addEventListener('submit', function(event) {";
  html += "      alert('Configuration Saved!\\nThe device will reboot and try to connect to WiFi.\\nIf something goes wrong, it will restart in setup mode.');";
  html += "    });";
  html += "function configManualRedirect() {";       
  html += "     window.location.href = '/wificonfigmanual';";
  html +=   "  }";
  html += "  </script>";

  html += "</body>";
  html += "</html>";

  webServer.send(200, "text/html", html);
}

void handleWifiConfigManual() {

  String page = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>WiFi Configuration</title>
    <style>
      body { font-family: Arial, sans-serif; text-align: center; padding: 20px; background-color: #f4f4f4; }
      .container { max-width: 400px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
      h1 { color: #333; }
      p { font-size: 18px; }
      .button { display: block; width: 100%; padding: 10px; margin: 10px 0; border: none; border-radius: 5px; font-size: 18px; text-decoration: none; cursor: pointer; text-align: center; }
      .btn-back { background-color: #007bff; color: white; }

      input[type=text], input[type=password] { width: 100%; padding: 10px; margin: 5px 0; border: 1px solid #ccc; border-radius: 5px; }
      input[type=submit] { width: 100%; padding: 10px; border: none; border-radius: 5px; background-color: #28a745; color: white; font-size: 18px; cursor: pointer; }
      input[type=submit]:hover { background-color: #218838; }
      label { font-weight: bold; display: block; margin-top: 10px; }
      #static_ip_fields { margin-top: 10px; text-align: left; }
    </style>
  </head>
  <body>
    <div class="container">
      <button class='button btn-back' onclick="location.href='/wifiinfo'">Indietro</button>
      <h1>WiFi Configuration</h1>
      <form id='wifiForm' action='/configure' method='POST'>
        <label>SSID:</label>
        <input type='text' name='ssid' required>
        <label>Password:</label>
        <input type='password' name='password' required>
        <label>Use static IP:</label>
        <input type='checkbox' name='static_ip' id='static_ip'>
        <div id='static_ip_fields' style='display:none;'>
          <label>IP:</label>
          <input type='text' name='ip' placeholder='192.168.1.100'>
          <label>Gateway:</label>
          <input type='text' name='gateway' placeholder='192.168.1.1'>
          <label>Subnet:</label>
          <input type='text' name='subnet' placeholder='255.255.255.0'>
        </div>
        <input type='submit' value='Save'>
      </form>
    </div>
    <script>
      document.getElementById('static_ip').addEventListener('change', function() {
        document.getElementById('static_ip_fields').style.display = this.checked ? 'block' : 'none';
      });
      document.getElementById('wifiForm').addEventListener('submit', function(event) {
        alert('Configuration Saved!\\nThe device will reboot and try to connect to WiFi.\\nIf something goes wrong, it will restart in setup mode.');
      });
    </script>
  </body>
  </html>
)rawliteral";

  webServer.send(200, "text/html", page);
}

// WiFi Configuration handling (receives data from POST form)
void handleConfigure() {
    // Form Data
    String ssid = webServer.arg("ssid");
    String password = webServer.arg("password");
    bool use_static_ip = webServer.hasArg("static_ip");
    String ip = webServer.arg("ip");
    String gateway = webServer.arg("gateway");
    String subnet = webServer.arg("subnet");

    if(wificonfig.config_set == 0){
      if (ssid.length() > 0 && password.length() > 0) {
        strncpy(wificonfig.wifi_ssid, ssid.c_str(), sizeof(wificonfig.wifi_ssid));
        strncpy(wificonfig.wifi_password, password.c_str(), sizeof(wificonfig.wifi_password));
        wificonfig.use_static_ip = use_static_ip;
        if (use_static_ip) {
            wificonfig.static_ip = parseIPAddress(ip);
            wificonfig.gateway = parseIPAddress(gateway);
            wificonfig.subnet = parseIPAddress(subnet);
        }
        wificonfig.config_set = 1;
      } else {
        webServer.send(400, "text/plain", "SSID and Password are required.");
        return;
      }
    }else{
      if (ip.length() > 0 && gateway.length() > 0 && subnet.length() > 0) {
        wificonfig.static_ip = parseIPAddress(ip);
        wificonfig.gateway = parseIPAddress(gateway);
        wificonfig.subnet = parseIPAddress(subnet);
        wificonfig.use_static_ip = true;
      } else {
        webServer.send(400, "text/plain", "IP Address, Gateway and Subnet are required.");
        return;
      }
    }
      
      saveWifiConfig();
      webServer.sendHeader("Location", "/", true);
      webServer.send(302, "text/plain", "WiFi config saved. Reboot...");
      Serial.println("Riavviando...");
      delay(1000);
      ESP.restart();
}

// Parsing IP from string
IPAddress parseIPAddress(String ip) {
    IPAddress result;
    result.fromString(ip);
    return result;
}

void handlePaginaIniziale() {
const char PAGE_GAMEPAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="UTF-8">
<title>Gamepad Test ESP32</title>
<style>
  body { 
    font-family: Arial, sans-serif; 
    text-align: center; 
    padding: 20px; 
    background-color: #f4f4f4; 
  }
  .container { 
    max-width: 400px; 
    margin: auto; 
    background: white; 
    padding: 20px; 
    border-radius: 10px; 
    box-shadow: 0 0 10px rgba(0,0,0,0.1); 
  }
  h2 { color: #333; }
  select, button { 
    font-size: 16px; 
    margin: 5px 0; 
    padding: 8px; 
    width: 100%; 
    border-radius: 5px; 
    border: 1px solid #ccc;
    box-sizing: border-box;
  }
  pre { 
    font-size: 16px; 
    text-align: left; 
    background: #f0f0f0; 
    padding: 10px; 
    border-radius: 5px; 
    margin-top: 15px; 
    white-space: pre-wrap;
  }
  .button { 
    display: block; 
    width: 100%; 
    padding: 10px; 
    margin: 15px 0 0 0; 
    border: none; 
    border-radius: 5px; 
    font-size: 18px; 
    text-decoration: none; 
    cursor: pointer; 
    text-align: center; 
  }
  .btn-log { background-color: #007bff; color: white; }

  .connection-status {
    margin: 15px 0;
    padding: 10px;
    border-radius: 8px;
    font-size: 18px;
    font-weight: bold;
    color: white;
  }

  .online {
    background-color: #28a745;
  }

  .offline {
    background-color: #dc3545;
  }

</style>
</head>
<body>

<div class="container">
  <h2>Seleziona Gamepad</h2>

  <div id="connectionStatus" class="connection-status offline">
  Connessione: OFFLINE
  </div>


  <select id="gamepadList"></select>
  <button onclick="refreshGamepads()">Aggiorna lista</button>

  <pre id="output">
X: 0
Y: 0
RX: 0
RY: 0
A: 0
B: 0
  </pre>

  <button class="button btn-log" onclick="location.href='/wifiinfo'">Info Wi-Fi</button>
</div>

<script>
let selectedGamepadIndex = null;
let ws = null;
let reconnectTimer = null;
const RECONNECT_DELAY = 1000;

const connectionStatus = document.getElementById("connectionStatus");

function setConnectionStatus(online) {
  if (online) {
    connectionStatus.textContent = "Connessione: ONLINE";
    connectionStatus.className = "connection-status online";
  } else {
    connectionStatus.textContent = "Connessione: OFFLINE";
    connectionStatus.className = "connection-status offline";
  }
}

function connectWebSocket() {

  // Evita di creare connessioni duplicate
  if (ws &&
      (ws.readyState === WebSocket.OPEN ||
       ws.readyState === WebSocket.CONNECTING)) {
    return;
  }

  setConnectionStatus(false);

  console.log("Tentativo connessione WebSocket...");

  ws = new WebSocket(`ws://${location.hostname}:81/`);
  ws.binaryType = "arraybuffer";

  ws.onopen = function() {
    console.log("WebSocket connessa");
    setConnectionStatus(true);

    // Se c'era un tentativo di riconnessione programmato, annullalo
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
  };

  ws.onclose = function() {
    console.log("WebSocket disconnessa");
    setConnectionStatus(false);

    scheduleReconnect();
  };

  ws.onerror = function(error) {
    console.log("Errore WebSocket");

    setConnectionStatus(false);
  };
}

function scheduleReconnect() {

  // Evita di creare più timer contemporaneamente
  if (reconnectTimer !== null) {
    return;
  }

  reconnectTimer = setTimeout(function() {
    reconnectTimer = null;
    connectWebSocket();
  }, RECONNECT_DELAY);
}

// Prima connessione
connectWebSocket();


function refreshGamepads() {
  const list = document.getElementById("gamepadList");
  list.innerHTML = "";
  const gamepads = navigator.getGamepads();
  for (let gp of gamepads) {
    if (gp) {
      const opt = document.createElement("option");
      opt.value = gp.index;
      opt.text = gp.id;
      list.appendChild(opt);
    }
  }
}

window.addEventListener("gamepadconnected", (e) => {
  selectedGamepadIndex = e.gamepad.index;
  refreshGamepads();
});

function floatToInt(val) {
  return Math.round(Math.max(-1, Math.min(1, val)) * 1000);
}

// Loop di invio a 50Hz (20ms)
setInterval(() => {
  const gamepads = navigator.getGamepads();
  const gp = gamepads[selectedGamepadIndex];
  if (!gp || ws.readyState !== WebSocket.OPEN) return;

  // Prepariamo un buffer binario di 6 interi a 16 bit (12 byte totali)
  const dataBuffer = new Int16Array(6);
  dataBuffer[0] = floatToInt(gp.axes[0]); // X
  dataBuffer[1] = floatToInt(gp.axes[1]); // Y
  dataBuffer[2] = floatToInt(gp.axes[2]); // RX
  dataBuffer[3] = floatToInt(gp.axes[3]); // RY
  dataBuffer[4] = gp.buttons[0].pressed ? 1 : 0; // A
  dataBuffer[5] = gp.buttons[1].pressed ? 1 : 0; // B

  ws.send(dataBuffer.buffer);

  document.getElementById("output").textContent = 
    `X: ${dataBuffer[0]}  Y: ${dataBuffer[1]}\nRX: ${dataBuffer[2]} RY: ${dataBuffer[3]}\nA: ${dataBuffer[4]}  B: ${dataBuffer[5]}`;
}, 20);

refreshGamepads();
</script>

</body>
</html>
)rawliteral";

webServer.send(200, "text/html", PAGE_GAMEPAD);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("Client %u connesso\n", num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("Client %u disconnesso\n", num);
      break;
    case WStype_BIN:
      if (length == sizeof(ControllerValues)) {
        memcpy(&controllerValues, payload, length);
        lastPacketTime = millis();


        /*Serial.printf("Ricevuto -> X:%d | Y:%d | RX:%d | RY:%d | A:%d | B:%d\n", 
                      controllerValues.x, 
                      controllerValues.y, 
                      controllerValues.rx, 
                      controllerValues.ry, 
                      controllerValues.a, 
                      controllerValues.b);*/
      } else {
        Serial.printf("Errore: Lunghezza dati non corretta (%u byte)\n", length);
      }
      break;
    default:
      break;
  }
}
void moveRonin(int16_t rx, int16_t ry) {
  // solo stato, niente send qui
  int16_t pan  = mapToSbus(rx);
  int16_t tilt = mapToSbus(ry);

  ronin.SetValue(1, pan);
  ronin.SetValue(2, tilt);
}

int16_t removeDeadzone(int16_t value, int deadzone) {

  if (abs(value) <= deadzone)
    return 0;

  if (value > 0)
    return map(value, deadzone, 1000, 0, 1000);

  return map(value, -1000, -deadzone, -1000, 0);
}

int16_t applyExpo(int16_t value, float expo) {

  float x = value / 1000.0f;

  float y =
      (1.0f - expo) * x +
      expo * x * x * x;

  return (int16_t)(y * 1000.0f);
}

int16_t mapToSbus(int16_t value) {

  const int deadzone = 30;

  // Elimina la deadzone e rimappa 50...1000 → 0...1000
  value = removeDeadzone(value, deadzone);

  if (value == 0)
    return sbusMID;

  // Curva cinematografica: precisa al centro
  value = applyExpo(value, 0.25f);

  // Low speed dimezza la velocità massima
  if (lowspeed)
    value /= 2;

  int16_t mapped = map(
    value,
    -1000,
    1000,
    sbusMIN,
    sbusMAX
  );

  return constrain(mapped, sbusMIN, sbusMAX);
}


void changeSpeed(int a) {
  if (a && !prevAState) {
    lowspeed = !lowspeed;
    Serial.println(lowspeed ? "LOW SPEED" : "NORMAL SPEED");
  }
  prevAState = a;
}

void startStopRecording(int b) {
  if (b && !prevBState) {
    cameraControl.startStopRecording();
  }
  prevBState = b;
}

int zoomCurve(int16_t value) {

  float x = abs(value) / 1000.0f;

  float y = x * x;

  return constrain((int)(y * 7.0f), 0, 7);
}


void zoomCamera(int16_t value) {

  const int deadzone = 50;

  if (abs(value) < deadzone) {
    zoomSpeed = 0;
    return;
  }

  zoomSpeed = value;
}

void roninTask(void *pv) {

  while (true) {

    ronin.Update();
    ronin.Send();

    vTaskDelay(pdMS_TO_TICKS(5)); // ~200Hz stabile
  }
}

void lancTask(void *pv) {

  while (true) {

    if (zoomSpeed > 0) {

      int speed = zoomCurve(zoomSpeed);

      if (speed > 0)
        cameraControl.zoomOut(speed);
    }

    else if (zoomSpeed < 0) {

      int speed = zoomCurve(zoomSpeed);

      if (speed > 0)
        cameraControl.zoomIn(speed);
    }

    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

void setup() {

  Serial.begin(115200);

  EEPROM.begin(sizeof(WifiConfig));

  ronin.begin(SBUS_PIN);

  loadWifiConfig();

  if (wificonfig.config_set != 1) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
  } else {
    startNormalMode();
  }

  webServer.on("/", handlePaginaIniziale);
  webServer.on("/wifiinfo", handleWiFiInfo);
  webServer.on("/wificonfig", handleWifiConfig);
  webServer.on("/configure", handleConfigure);
  webServer.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // ================= TASK RONIN CORE 0 =================
  xTaskCreatePinnedToCore(
    roninTask,
    "RONIN",
    4096,
    NULL,
    2,
    &roninTaskHandle,
    0
  );

  // ================= TASK LANC CORE 1 =================
  xTaskCreatePinnedToCore(
    lancTask,
    "LANC",
    4096,
    NULL,
    1,
    &lancTaskHandle,
    1
  );
}

void loop() {

  webServer.handleClient();
  webSocket.loop();

  if (millis() - lastPacketTime > CONTROL_TIMEOUT) {
    controllerValues.rx = 0;
    controllerValues.ry = 0;
    controllerValues.y = 0;
  }


  moveRonin(controllerValues.rx, controllerValues.ry);
  changeSpeed(controllerValues.a);
  zoomCamera(controllerValues.y);
  startStopRecording(controllerValues.b);
}


