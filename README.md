# 🎮 RoninControl

**RoninControl** è un sistema di controllo wireless basato su **ESP32** per comandare un **DJI Ronin** e una videocamera compatibile **LANC** tramite un normale gamepad.

Il controllo avviene direttamente dal browser: il gamepad viene letto tramite la **Gamepad API** e i comandi vengono inviati all'ESP32 tramite **WebSocket**.

## ✨ Funzionalità

- 🎮 Controllo tramite gamepad
- 📡 Comunicazione Wi-Fi tramite WebSocket
- 🤖 Controllo Ronin tramite SBUS
- 🎥 Controllo videocamera tramite LANC
- 🔭 Controllo dello zoom a velocità variabile
- 🐢 Modalità **Low Speed** per movimenti più precisi
- ⏺️ Start / Stop della registrazione
- 🎚️ Deadzone ed Expo per un controllo più fluido
- ⚙️ Configurazione Wi-Fi tramite interfaccia web
- 💾 Salvataggio della configurazione Wi-Fi in EEPROM
- 📍 Supporto per IP statico
- 🛡️ Timeout automatico in caso di perdita della connessione

## 🎮 Controlli

| Controller | Funzione |
|---|---|
| Stick / Axis 1 | Pan / Tilt Ronin |
| Stick / Axis 2 | Controllo Ronin |
| Asse Y | Zoom videocamera |
| **A** | Attiva / disattiva Low Speed |
| **B** | Start / Stop registrazione |

## 🔌 Pinout

| ESP32 | Funzione |
|---|---|
| GPIO 12 | LANC Command |
| GPIO 13 | LANC |
| GPIO 14 | SBUS Ronin |

## 📡 Configurazione Wi-Fi

Al primo avvio l'ESP32 crea automaticamente una rete:

```text
SSID: RoninControl_Setup
IP:   192.168.4.1
```
Collegati alla rete e apri:
http://192.168.4.1

Da qui puoi configurare:
SSID e password della rete
DHCP oppure IP statico
Gateway
Subnet
La configurazione viene salvata nella EEPROM e utilizzata automaticamente ai successivi avvii.
Se l'ESP32 non riesce a collegarsi alla rete configurata, torna automaticamente alla modalità di configurazione.

## Interfaccia Web
L'interfaccia permette di:
selezionare il gamepad
verificare lo stato della connessione
visualizzare i valori degli assi
visualizzare lo stato dei pulsanti
accedere alle impostazioni Wi-Fi
I dati del controller vengono inviati all'ESP32 tramite WebSocket a circa 50 Hz.

## Architettura

              🎮 GAMEPAD
                   │
                   ▼
              🌐 BROWSER
                   │
              WebSocket
                   │
                   ▼
               ⚡ ESP32
              ┌────┴────┐
              │         │
             SBUS      LANC
              │         │
              ▼         ▼
           🤖 Ronin   🎥 Camera
           
## 📦 Librerie

Nel repository sono incluse le librerie necessarie:

- `WebSocketsServer`
- `Ronin_SBUS`
- `LANC_CAM_CONTROL`

Le librerie devono essere copiate nella cartella `libraries` della propria installazione Arduino.

Su Windows, il percorso è normalmente:

`Documenti/Arduino/libraries/`

Quindi la struttura dovrà essere:

- `Documenti/Arduino/libraries/WebSocketsServer/`
- `Documenti/Arduino/libraries/Ronin_SBUS/`
- `Documenti/Arduino/libraries/LANC_CAM_CONTROL/`

Dopo aver copiato le librerie, riavvia Arduino IDE.


## 🚀 Installazione
Clona o scarica il repository.

Copia le tre librerie dalla cartella libraries/ del progetto in Documenti/Arduino/libraries/.

Apri RoninControl.ino con Arduino IDE.

Seleziona la tua scheda ESP32.

Carica lo sketch.

Collegati alla rete RoninControl_Setup.

Apri 192.168.4.1 e configura il Wi-Fi.

Collega il gamepad al dispositivo utilizzato per il controllo.

Apri l'indirizzo IP assegnato all'ESP32.
## ⚠️ Note
Progetto pensato principalmente per uso DIY e sperimentale.
Verifica sempre la compatibilità del tuo Ronin e della videocamera LANC e controlla attentamente i collegamenti elettrici prima dell'utilizzo.
