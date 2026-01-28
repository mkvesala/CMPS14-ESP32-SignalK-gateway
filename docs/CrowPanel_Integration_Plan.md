# CMPS14 Kompassi + Crow Panel 2.1" Integraatiosuunnitelma

**Dokumentin versio:** 1.0
**Päivämäärä:** 2026-01-28
**Status:** Suunnitteluvaihe (ei vielä toteutusta)

---

## Dokumentin tarkoitus

Tämä dokumentti sisältää täydellisen suunnitelman Elecrow Crow Panel 2.1" pyöreän kosketusnäytön integroimiseksi olemassa olevaan CMPS14-ESP32-SignalK-gateway kompassijärjestelmään. Dokumentti on tarkoitettu jatkokehityksen pohjaksi ja sisältää kaiken session aikana kerätyn kontekstin.

---

## Sisällysluettelo

1. [Projektin tausta ja tavoitteet](#1-projektin-tausta-ja-tavoitteet)
2. [Järjestelmien tekninen analyysi](#2-järjestelmien-tekninen-analyysi)
3. [Kommunikaatiovaihtoehtojen arviointi](#3-kommunikaatiovaihtoehtojen-arviointi)
4. [Suositeltu arkkitehtuuri](#4-suositeltu-arkkitehtuuri)
5. [Kolmivaiheinen toteutussuunnitelma](#5-kolmivaiheinen-toteutussuunnitelma)
6. [Vaiheen 1 yksityiskohtainen suunnitelma](#6-vaiheen-1-yksityiskohtainen-suunnitelma)
7. [Getting Started -ohje](#7-getting-started--ohje)
8. [Liitteet](#8-liitteet)

---

## 1. Projektin tausta ja tavoitteet

### 1.1 Lähtötilanne

**Olemassa oleva järjestelmä:**
- CMPS14-ESP32-SignalK-gateway digitaalinen kompassi
- GitHub: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/
- Toimii itsenäisesti ESP32 + CMPS14 sensori + 16x2 LCD -kokoonpanolla
- Lähettää kompassidataa SignalK-palvelimelle WebSocket-yhteydellä

**Uusi komponentti:**
- Elecrow Crow Panel 2.1" HMI ESP32 Rotary Display
- 480×480 IPS pyöreä kosketusnäyttö
- ESP32-S3-N16R8 prosessori
- Rotary encoder + painike
- Wiki: https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html

### 1.2 Tavoitteet

**Vaihe 1 (Peruskäyttötila):**
- Kompassiruusu heading-näytöllä
- Pitch ja roll näyttäminen
- True/Magnetic heading tuki

**Vaihe 2 (Kalibrointitila):**
- Kalibrointitilojen hallinta (USE, FULL_AUTO, AUTO, MANUAL)
- Kalibrointi-statuksen visualisointi
- Kalibrointiprofiilin tallennus

**Vaihe 3 (Konfigurointi):**
- Kaikki WebUIManagerin toiminnot Crow Panelissa
- Installation offset, magnetic variation, heading mode
- Deviation-mittausten syöttö

### 1.3 Rajoitteet ja reunaehdot

1. **Itsenäisyys:** Crow Panel EI saa olla riippuvainen SignalK-palvelimesta
2. **Taaksepäin yhteensopivuus:** Nykyinen kompassitoteutus ei saa häiriintyä
3. **Minimaaliset muutokset:** Kompassijärjestelmän muutokset pidettävä minimissä
4. **Suorituskyky:** Stack allocation, String-käsittelyn välttäminen
5. **Muistitehokkuus:** Binääriprotokolla JSON:n sijaan

---

## 2. Järjestelmien tekninen analyysi

### 2.1 CMPS14-ESP32-SignalK-gateway arkkitehtuuri

#### Hakemistorakenne
```
CMPS14-ESP32-SignalK-gateway/
├── CMPS14-ESP32-SignalK-gateway.ino    # Entry point
├── CMPS14Application.cpp/h              # Orkestroija
├── CMPS14Sensor.cpp/h                   # I2C sensorirajapinta
├── CMPS14Processor.cpp/h                # Kompassilogiikka
├── CMPS14Preferences.cpp/h              # NVS-tallennnus
├── SignalKBroker.cpp/h                  # WebSocket SignalK:lle
├── WebUIManager.cpp/h                   # HTTP-palvelin + Web UI
├── DisplayManager.cpp/h                 # LCD + LED-ohjaus
├── harmonic.cpp/h                       # Poikkeaman laskenta
├── CalMode.h                            # Kalibrointi-enumit
├── WifiState.h                          # WiFi-tila enumit
├── version.h                            # Versiotiedot
└── secrets.example.h                    # Konfiguraatiomallit
```

#### Komponenttien vastuut

```
CMPS14Application (Orkestroija)
│
├── CMPS14Sensor
│   └── I2C-lukeminen (0x60, SDA=GPIO16, SCL=GPIO17)
│   └── Raakadata: angle (16-bit), pitch (8-bit), roll (8-bit)
│
├── CMPS14Processor
│   └── Installation offset
│   └── Smoothing (α=0.15)
│   └── Deviation lookup (harmoninen malli)
│   └── Magnetic variation (SignalK tai manuaalinen)
│   └── True/Magnetic heading laskenta
│   └── Kalibroinnin hallinta
│
├── CMPS14Preferences
│   └── NVS-tallennnus kaikille asetuksille
│
├── SignalKBroker
│   └── WebSocket-yhteys SignalK-palvelimelle
│   └── Delta-viestien lähetys (~10 Hz)
│   └── Magnetic variation vastaanotto
│
├── DisplayManager
│   └── 16x2 LCD (I2C 0x27)
│   └── LED-indikaattorit
│
└── WebUIManager
    └── HTTP-palvelin (portti 80)
    └── Session-autentikointi
    └── REST API konfiguraatiolle
```

#### Datavirta

```
CMPS14 Sensori (I2C)
    │
    ▼ Raw: angle_deg, pitch_deg, roll_deg
    │
Installation Offset (+/- astetta)
    │
    ▼
Smoothing (alfa=0.15, 85% historia)
    │
    ▼ compass_deg (Compass Heading)
    │
Deviation Lookup (harmoninen malli)
    │
    ▼ heading_deg (Magnetic Heading)
    │
Magnetic Variation
    │
    ▼ heading_true_deg (True Heading)
    │
Radiaani-muunnos
    │
    ▼
SignalK Delta (~10 Hz, deadband 0.25°)
```

#### Taajuudet

| Toiminto | Taajuus |
|----------|---------|
| Sensorin lukeminen | ~21 Hz (47 ms) |
| SignalK delta-lähetys | ~10 Hz (101 ms) |
| Pitch/Roll min/max | ~1 Hz (997 ms) |
| Kalibrointi-status | ~2 Hz (499 ms) |

#### WebUIManager päätepisteet

| Pääte | Toiminto |
|-------|----------|
| `/status` | JSON-statustiedot |
| `/offset/set` | Installation offset |
| `/dev8/set` | 8 deviation-mittausta |
| `/calmode/set` | Kalibrointitila bootissa |
| `/magvar/set` | Manuaalinen magnetic variation |
| `/heading/mode` | True/Magnetic valinta |
| `/cal/on`, `/cal/off` | Kalibroinnin ohjaus |
| `/store/on` | Kalibrointiprofiilin tallennus |
| `/reset/on` | CMPS14 tehdasasetus |
| `/level` | Pitch/Roll nollaus |
| `/restart` | ESP32 uudelleenkäynnistys |

#### Kalibrointitilat (CalMode)

```cpp
enum class CalMode : uint8_t {
    USE       = 0,  // Normaali käyttö
    FULL_AUTO = 1,  // CMPS14 autokalibrointi + autosave
    AUTO      = 2,  // Autokalibrointi, manuaalinen save
    MANUAL    = 3   // Käyttäjän ohjaama kalibrointi
};
```

### 2.2 Crow Panel 2.1" tekniset tiedot

| Ominaisuus | Spesifikaatio |
|------------|---------------|
| **MCU** | ESP32-S3-N16R8 (dual-core 240 MHz) |
| **Flash** | 16 MB |
| **PSRAM** | 8 MB |
| **Näyttö** | 480×480 IPS, ST7701 ohjain |
| **Kosketuspaneeli** | Kapasitiivinen |
| **Rotary encoder** | Pyöritys + painike |
| **WiFi** | 2.4 GHz 802.11 b/g/n |
| **Bluetooth** | BLE 5.0 |
| **Liitännät** | UART, I2C, FPC, USB-C |

**Ohjelmistotuki:**
- Arduino IDE + Arduino_GFX_Library
- LVGL 8.x / 9.x
- SquareLine Studio
- ESP-IDF, MicroPython, ESPHome

---

## 3. Kommunikaatiovaihtoehtojen arviointi

### 3.1 Vertailutaulukko

| Kriteeri | WiFi (WebSocket) | ESP-NOW | BLE | UART |
|----------|------------------|---------|-----|------|
| Latenssi | 10-50 ms | 1-5 ms | 10-30 ms | <1 ms |
| Infrastruktuuritarve | Kyllä | Ei | Ei | Kaapeli |
| Kantama | Verkon kattavuus | ~50m sisällä | ~30m | Kaapelin pituus |
| Muutokset kompassiin | Minimaaliset | Kohtalaiset | Merkittävät | Pienet |
| WiFi-yhteensopivuus | N/A | Sama kanava | Täysi | Täysi |
| Protokollan monimutkaisuus | JSON parsing | Binääri | GATT | Binääri |
| Suorituskyky | Kohtuullinen | Erinomainen | Hyvä | Erinomainen |

### 3.2 ESP-NOW + WiFi yhteiskäyttö

**Tärkeä löydös:** ESP-NOW ja WiFi VOIVAT toimia samanaikaisesti:

1. **Sama kanava -vaatimus:** ESP-NOW ja WiFi AP:n on käytettävä samaa kanavaa
2. **WiFi-moodi:** Aseta `WIFI_AP_STA` (ei pelkkä `WIFI_STA`)
3. **Kanavan synkronointi:** Crow Panel asetetaan samalle kanavalle kuin kompassin WiFi-yhteys

**Lähteet:**
- [ThingPulse: ESP-NOW ja WiFi samanaikaisesti](https://thingpulse.com/esp32-espnow-wifi-simultaneous-communication/)
- [Random Nerd Tutorials: ESP-NOW + Web Server](https://randomnerdtutorials.com/esp32-esp-now-wi-fi-web-server/)
- [ESP-IDF RF Coexistence](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/coexist.html)

### 3.3 Suositus

**Ensisijainen: ESP-NOW**
- Matala latenssi (1-5 ms) → sujuva kompassianimaatio
- Ei vaadi WiFi-infrastruktuuria (veneessä tärkeää)
- Binääriprotokolla → ei String/JSON overheadia
- Toimii WiFi:n rinnalla samalla kanavalla

**Vaihtoehtoinen: UART**
- Jos kaapelointi sopii asennukseen
- Äärimmäinen luotettavuus ja nopeus

---

## 4. Suositeltu arkkitehtuuri

### 4.1 Järjestelmäkaavio

```
┌─────────────────────────────────────────────────────────────────┐
│                    JÄRJESTELMÄARKKITEHTUURI                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   CMPS14 Kompassi (ESP32)          Crow Panel 2.1" (ESP32-S3)  │
│   ┌─────────────────────┐          ┌─────────────────────┐     │
│   │                     │          │                     │     │
│   │  CMPS14Sensor       │          │     LVGL UI         │     │
│   │       │             │          │        │            │     │
│   │       ▼             │          │        ▼            │     │
│   │  CMPS14Processor    │          │  ScreenManager      │     │
│   │       │             │          │        │            │     │
│   │       ├─────────────┼──WiFi────┼→ SignalK Server     │     │
│   │       │             │          │                     │     │
│   │       ▼             │          │        ▲            │     │
│   │  ESPNowBridge ══════╪══════════╪→ ESPNowReceiver     │     │
│   │  (uusi moduuli)     │  ~10 Hz  │        │            │     │
│   │                     │  ~1-5ms  │        ▼            │     │
│   │  WebUIManager       │          │  DataManager        │     │
│   │  (ennallaan)        │          │                     │     │
│   │                     │          │                     │     │
│   └─────────────────────┘          └─────────────────────┘     │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 Binääriprotokolla

#### Kompassilta Crow Panelille (20 tavua)

```cpp
#pragma pack(push, 1)
struct CompassDataPacket {
    uint8_t  msg_type;           // 0x01 = compass data
    uint8_t  flags;              // bit0: heading_true_valid
                                 // bit1: cal_active
    uint16_t heading_deg_x10;    // 0-3599 (0.0-359.9°, *10)
    int16_t  pitch_deg_x10;      // -900..+900 (-90.0..+90.0°, *10)
    int16_t  roll_deg_x10;       // -1800..+1800 (-180.0..+180.0°, *10)
    uint16_t heading_true_x10;   // 0-3599 (true heading *10)
    int16_t  mag_var_x10;        // -1800..+1800 (variation *10)
    uint8_t  cal_status;         // Kalibrointi-status byte
                                 // bits 0-1: mag (0-3)
                                 // bits 2-3: acc (0-3)
                                 // bits 4-5: gyr (0-3)
                                 // bits 6-7: sys (0-3)
    uint8_t  cal_mode;           // CalMode enum (0-3)
    uint16_t sequence;           // Juokseva numero
    uint8_t  reserved[4];        // Tulevaa käyttöä varten
};
#pragma pack(pop)
```

#### Crow Panelilta kompassille (8 tavua)

```cpp
#pragma pack(push, 1)
struct DisplayCommand {
    uint8_t  msg_type;           // 0x10 = command
    uint8_t  cmd;                // Komento-ID (alla)
    int16_t  param1;             // Parametri 1
    int16_t  param2;             // Parametri 2
    uint16_t sequence;           // Vastaus-sekvenssi
};
#pragma pack(pop)

// Komento-ID:t
enum CrowPanelCmd : uint8_t {
    CMD_NONE            = 0x00,
    // Kalibrointi (Vaihe 2)
    CMD_START_CAL       = 0x10,  // param1 = CalMode
    CMD_STOP_CAL        = 0x11,
    CMD_STORE_CAL       = 0x12,
    // Konfigurointi (Vaihe 3)
    CMD_SET_OFFSET      = 0x20,  // param1 = offset * 10
    CMD_SET_MAG_VAR     = 0x21,  // param1 = variation * 10
    CMD_SET_HDG_MODE    = 0x22,  // param1 = 0(mag) / 1(true)
    CMD_LEVEL_ATTITUDE  = 0x30,
    CMD_RESTART         = 0xFF
};
```

### 4.3 Muutokset kompassijärjestelmään

**Uusi tiedosto:**
```
ESPNowBridge.cpp/h    # ~150-200 riviä, irrotettava moduuli
```

**Muutokset olemassa oleviin:**
```
CMPS14Application.cpp  # ~20 riviä (alustus + loop-kutsu)
secrets.h              # ENABLE_ESPNOW_BRIDGE flag
```

**Conditional compilation:**
```cpp
// secrets.h
#define ENABLE_ESPNOW_BRIDGE 1  // 0 = poistaa ESP-NOW-koodin käännöksestä

// ESPNowBridge.cpp
#if ENABLE_ESPNOW_BRIDGE
// ... koodi ...
#endif
```

---

## 5. Kolmivaiheinen toteutussuunnitelma

### Vaihe 1: Peruskäyttötila (MVP)

**Kompassijärjestelmä:**
- ESP-NOW alustus WiFi:n rinnalle
- CompassDataPacket lähetys 10 Hz
- Conditional compilation

**Crow Panel:**
- ESP-NOW vastaanotto
- LVGL UI: kompassiruusu, heading, pitch, roll
- DataManager datan välimuistiin

**Ei vielä:**
- Komentojen lähetystä
- Konfigurointia

### Vaihe 2: Kalibrointitila

**Kompassijärjestelmä:**
- DisplayCommand vastaanotto
- Kalibrointikomentojen käsittely

**Crow Panel:**
- Kalibrointi-UI näkymä
- Status-visualisointi (mag/acc/sys pylväät)
- Komentojen lähetys (START_CAL, STOP_CAL, STORE_CAL)
- Rotary encoder tilan valintaan

### Vaihe 3: Konfigurointi

**Kompassijärjestelmä:**
- Loput konfigurointikomennot

**Crow Panel:**
- Asetussivut (Installation offset, Mag var, Heading mode)
- Deviation-mittausten syöttö
- Rotary encoder arvojen säätöön
- Paikallinen NVS-tallennnus (Crow Panelin omat asetukset)

---

## 6. Vaiheen 1 yksityiskohtainen suunnitelma

### 6.1 ESPNowBridge.h (kompassijärjestelmä)

```cpp
#ifndef ESPNOW_BRIDGE_H
#define ESPNOW_BRIDGE_H

#include <Arduino.h>
#include <esp_now.h>

#pragma pack(push, 1)
struct CompassDataPacket {
    uint8_t  msg_type;           // 0x01
    uint8_t  flags;
    uint16_t heading_deg_x10;
    int16_t  pitch_deg_x10;
    int16_t  roll_deg_x10;
    uint16_t heading_true_x10;
    int16_t  mag_var_x10;
    uint8_t  cal_status;
    uint8_t  cal_mode;
    uint16_t sequence;
    uint8_t  reserved[4];
};
#pragma pack(pop)

class ESPNowBridge {
public:
    bool begin();
    void loop();

    // Setterit - kutsutaan CMPS14Processorilta
    void setHeadingDeg(float deg);
    void setHeadingTrueDeg(float deg);
    void setPitchDeg(float deg);
    void setRollDeg(float deg);
    void setMagVarDeg(float deg);
    void setCalStatus(uint8_t status);
    void setCalMode(uint8_t mode);
    void setCalActive(bool active);
    void setHeadingTrueValid(bool valid);

    // Pairing
    void setPeerAddress(const uint8_t* mac);
    bool hasPeer() const { return _peer_set; }

private:
    CompassDataPacket _packet;
    uint8_t _peer_mac[6];
    bool _peer_set = false;
    uint16_t _sequence = 0;
    unsigned long _last_tx_ms = 0;

    static constexpr unsigned long TX_INTERVAL_MS = 100;  // 10 Hz
    static constexpr uint8_t MSG_TYPE_COMPASS = 0x01;

    void sendPacket();
    static void onDataSent(const uint8_t* mac, esp_now_send_status_t status);
    static void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);
};

#endif
```

### 6.2 ESPNowBridge.cpp (runko)

```cpp
#include "ESPNowBridge.h"

#if ENABLE_ESPNOW_BRIDGE

#include <WiFi.h>

static ESPNowBridge* _instance = nullptr;

bool ESPNowBridge::begin() {
    _instance = this;

    // WiFi mode: AP+STA mahdollistaa ESP-NOW + WiFi
    WiFi.mode(WIFI_AP_STA);

    if (esp_now_init() != ESP_OK) {
        return false;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    memset(&_packet, 0, sizeof(_packet));
    _packet.msg_type = MSG_TYPE_COMPASS;

    return true;
}

void ESPNowBridge::loop() {
    if (!_peer_set) return;

    unsigned long now = millis();
    if (now - _last_tx_ms >= TX_INTERVAL_MS) {
        _last_tx_ms = now;
        sendPacket();
    }
}

void ESPNowBridge::sendPacket() {
    _packet.sequence = _sequence++;
    esp_now_send(_peer_mac, (uint8_t*)&_packet, sizeof(_packet));
}

void ESPNowBridge::setHeadingDeg(float deg) {
    _packet.heading_deg_x10 = (uint16_t)(deg * 10.0f + 0.5f);
}

void ESPNowBridge::setHeadingTrueDeg(float deg) {
    _packet.heading_true_x10 = (uint16_t)(deg * 10.0f + 0.5f);
}

void ESPNowBridge::setPitchDeg(float deg) {
    _packet.pitch_deg_x10 = (int16_t)(deg * 10.0f);
}

void ESPNowBridge::setRollDeg(float deg) {
    _packet.roll_deg_x10 = (int16_t)(deg * 10.0f);
}

void ESPNowBridge::setMagVarDeg(float deg) {
    _packet.mag_var_x10 = (int16_t)(deg * 10.0f);
}

void ESPNowBridge::setCalStatus(uint8_t status) {
    _packet.cal_status = status;
}

void ESPNowBridge::setCalMode(uint8_t mode) {
    _packet.cal_mode = mode;
}

void ESPNowBridge::setCalActive(bool active) {
    if (active) _packet.flags |= 0x02;
    else _packet.flags &= ~0x02;
}

void ESPNowBridge::setHeadingTrueValid(bool valid) {
    if (valid) _packet.flags |= 0x01;
    else _packet.flags &= ~0x01;
}

void ESPNowBridge::setPeerAddress(const uint8_t* mac) {
    memcpy(_peer_mac, mac, 6);

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = WiFi.channel();
    peer_info.encrypt = false;

    if (esp_now_add_peer(&peer_info) == ESP_OK) {
        _peer_set = true;
    }
}

void ESPNowBridge::onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
    // Diagnostiikka tarvittaessa
}

void ESPNowBridge::onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    // Vaihe 2: Komentojen käsittely
    if (_instance && len >= 2 && data[0] == 0x10) {
        // DisplayCommand parsing
    }
}

#endif // ENABLE_ESPNOW_BRIDGE
```

### 6.3 CMPS14Application.cpp muutokset

```cpp
// Lisää includet
#include "ESPNowBridge.h"

// Jäsenmuuttuja CMPS14Application-luokkaan
#if ENABLE_ESPNOW_BRIDGE
ESPNowBridge _espnow_bridge;
#endif

// setup()-funktioon, WiFi-yhdistämisen JÄLKEEN:
#if ENABLE_ESPNOW_BRIDGE
if (_espnow_bridge.begin()) {
    // Crow Panel MAC (kovakoodattu tai NVS:stä)
    uint8_t crow_mac[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};
    _espnow_bridge.setPeerAddress(crow_mac);
    Serial.println("ESP-NOW bridge initialized");
}
#endif

// loop()-funktioon, sensorin lukemisen jälkeen:
#if ENABLE_ESPNOW_BRIDGE
_espnow_bridge.setHeadingDeg(compass.getHeadingDeg());
_espnow_bridge.setHeadingTrueDeg(compass.getHeadingTrueDeg());
_espnow_bridge.setPitchDeg(compass.getPitchDeg());
_espnow_bridge.setRollDeg(compass.getRollDeg());
_espnow_bridge.setMagVarDeg(compass.getMagneticVariationDeg());
_espnow_bridge.setCalStatus(compass.getCalStatusByte());
_espnow_bridge.setCalMode((uint8_t)compass.getCalibrationModeRuntime());
_espnow_bridge.setCalActive(compass.isCalibrationActive());
_espnow_bridge.setHeadingTrueValid(compass.isSendHeadingTrue());
_espnow_bridge.loop();
#endif
```

### 6.4 Crow Panel projektirakenne

```
CrowPanel_Compass/
├── CrowPanel_Compass.ino       # Entry point
├── src/
│   ├── ESPNowReceiver.h/cpp    # ESP-NOW vastaanotto
│   ├── DataManager.h/cpp       # Datan hallinta
│   ├── ScreenManager.h/cpp     # Näyttöjen hallinta
│   └── screens/
│       ├── CompassScreen.h/cpp # Kompassi-UI (Vaihe 1)
│       ├── CalibScreen.h/cpp   # Kalibrointi (Vaihe 2)
│       └── ConfigScreen.h/cpp  # Asetukset (Vaihe 3)
├── ui/                         # SquareLine Studio export
│   ├── ui.h/c
│   ├── ui_helpers.h/c
│   └── images/
│       └── compass_rose.c
└── platformio.ini
```

### 6.5 Crow Panel ESPNowReceiver.h

```cpp
#ifndef ESPNOW_RECEIVER_H
#define ESPNOW_RECEIVER_H

#include <Arduino.h>
#include <esp_now.h>

#pragma pack(push, 1)
struct CompassDataPacket {
    uint8_t  msg_type;
    uint8_t  flags;
    uint16_t heading_deg_x10;
    int16_t  pitch_deg_x10;
    int16_t  roll_deg_x10;
    uint16_t heading_true_x10;
    int16_t  mag_var_x10;
    uint8_t  cal_status;
    uint8_t  cal_mode;
    uint16_t sequence;
    uint8_t  reserved[4];
};
#pragma pack(pop)

class ESPNowReceiver {
public:
    bool begin(const uint8_t* compass_mac);

    bool hasNewData();
    void getData(CompassDataPacket& out);

    unsigned long getLastRxTime() const { return _last_rx_ms; }
    bool isConnected(unsigned long timeout_ms = 500) const {
        return (millis() - _last_rx_ms) < timeout_ms;
    }

private:
    static ESPNowReceiver* _instance;
    CompassDataPacket _packet;
    volatile bool _new_data = false;
    volatile unsigned long _last_rx_ms = 0;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

    static void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);
};

#endif
```

### 6.6 Crow Panel DataManager.h

```cpp
#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include "ESPNowReceiver.h"

struct CompassState {
    float heading_deg;
    float heading_true_deg;
    float pitch_deg;
    float roll_deg;
    float mag_var_deg;

    uint8_t cal_mag;   // 0-3
    uint8_t cal_acc;   // 0-3
    uint8_t cal_sys;   // 0-3
    uint8_t cal_mode;  // CalMode enum

    bool heading_true_valid;
    bool cal_active;
    bool connected;

    uint16_t sequence;
};

class DataManager {
public:
    void begin(ESPNowReceiver* receiver);
    void update();

    const CompassState& getState() const { return _state; }

private:
    ESPNowReceiver* _receiver;
    CompassState _state;

    void decodePacket(const CompassDataPacket& pkt);
};

#endif
```

### 6.7 Käyttöliittymäsuunnitelma (480×480 pyöreä)

```
            ┌────────────────────────────┐
           /                              \
          /      ┌──────────────────┐      \
         /       │    HDG: 247°T    │       \
        /        │    (tai 241°M)   │        \
       │         └──────────────────┘         │
       │                                       │
       │              N                        │
       │           NW   NE                     │
       │                                       │
       │         W    [▲]    E                 │
       │              │                        │
       │           SW   SE                     │
       │              S                        │
       │                                       │
       │    ┌─────────────┬─────────────┐     │
       │    │ PITCH: +3°  │ ROLL: -2°   │     │
        \   │ ▂▃▄▅▆▅▄▃▂   │ ▂▃▄█▄▃▂     │    /
         \  └─────────────┴─────────────┘   /
          \                                /
           \    [🔗 CONNECTED | 10Hz]     /
            └────────────────────────────┘
```

### 6.8 UI-elementit LVGL:llä

| Elementti | LVGL-tyyppi | Päivitys |
|-----------|-------------|----------|
| Kompassiruusu | `lv_img` + `lv_img_set_angle()` | Heading muuttuessa |
| Heading-teksti | `lv_label` | 10 Hz |
| Pitch-palkki | `lv_bar` | 10 Hz |
| Roll-palkki | `lv_bar` | 10 Hz |
| Status | `lv_label` | Yhteyden tila muuttuessa |

### 6.9 Kompassiruusun rotaatio

```cpp
// Pyörivä ruusu, kiinteä nuoli (suositus)
void updateCompass(float heading_deg) {
    // LVGL: 0.1° resoluutio, negatiivinen = myötäpäivään
    int16_t angle = (int16_t)(-heading_deg * 10);
    lv_img_set_angle(compass_rose_img, angle);
}
```

### 6.10 SquareLine Studio asetukset

```
Project Settings:
├── Resolution: 480 × 480
├── Color depth: 16-bit
├── Shape: Round
├── LVGL version: 8.4
└── Export: Arduino

Assets:
├── compass_rose.png (480×480, läpinäkyvä tausta)
└── Font: Montserrat Bold 24/32/48
```

---

## 7. Getting Started -ohje

### 7.1 Esivaatimukset

**Laitteisto:**
- [x] Toimiva CMPS14-ESP32-SignalK-gateway
- [ ] Elecrow Crow Panel 2.1"
- [ ] USB-C kaapeli

**Ohjelmistot:**
- [ ] Arduino IDE 2.x tai PlatformIO
- [ ] ESP32 Board Support >= 2.0.0
- [ ] LVGL 8.4.0
- [ ] Arduino_GFX_Library
- [ ] SquareLine Studio (UI-suunnittelu)

### 7.2 Arduino IDE asetukset Crow Panelille

```
Board: ESP32S3 Dev Module
Upload Speed: 921600
USB Mode: Hardware CDC and JTAG
USB CDC On Boot: Enabled
Flash Size: 16MB (128Mb)
Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
PSRAM: OPI PSRAM
```

### 7.3 Vaihe 1: Crow Panel perustestaus

```cpp
// CrowPanel_HelloWorld.ino
#include <Arduino_GFX_Library.h>

#define TFT_BL 38

Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18, 17, 16, 21, 4, 3, 2, 1,
    10, 9, 40, 41, 42, 14,
    45, 0, -1, -1,
    1, 10, 8, 50, 20, 10, 8, 50, 20);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0, true);

void setup() {
    gfx->begin();
    gfx->fillScreen(BLACK);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(3);
    gfx->setCursor(150, 220);
    gfx->println("Hello Compass!");

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}

void loop() {}
```

### 7.4 Vaihe 2: ESP-NOW testiyhteys

**A. Crow Panel (vastaanottaja) - MAC-osoitteen selvitys:**

```cpp
#include <WiFi.h>
#include <esp_now.h>

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);

    Serial.print("Crow Panel MAC: ");
    Serial.println(WiFi.macAddress());
    // Kopioi tämä kompassin koodiin!

    esp_now_init();
    esp_now_register_recv_cb([](const uint8_t* mac, const uint8_t* data, int len) {
        Serial.printf("Received %d bytes\n", len);
    });
}

void loop() {}
```

**B. Kompassi (lähettäjä) - testitila:**

```cpp
// Lisää secrets.h:
#define ENABLE_ESPNOW_BRIDGE 1
uint8_t CROW_PANEL_MAC[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};
```

### 7.5 Vaihe 3: Tarkistuslista

- [ ] Crow Panel näyttää "Hello Compass"
- [ ] MAC-osoite kopioitu kompassikoodiin
- [ ] ESP-NOW yhteys toimii (Serial monitorissa dataa)
- [ ] Heading päivittyy Crow Panelille
- [ ] Taajuus ~10 Hz
- [ ] WiFi→SignalK edelleen toimii

---

## 8. Liitteet

### 8.1 Lähteet ja linkit

- [CMPS14-ESP32-SignalK-gateway GitHub](https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/)
- [Elecrow Crow Panel Wiki](https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html)
- [ThingPulse: ESP-NOW + WiFi](https://thingpulse.com/esp32-espnow-wifi-simultaneous-communication/)
- [Random Nerd Tutorials: ESP-NOW + Web Server](https://randomnerdtutorials.com/esp32-esp-now-wi-fi-web-server/)
- [ESP-IDF RF Coexistence](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/coexist.html)
- [LVGL dokumentaatio](https://docs.lvgl.io/)
- [SquareLine Studio](https://squareline.io/)

### 8.2 Arvioidut koodimuutokset

| Kohde | Uutta koodia | Muutoksia |
|-------|--------------|-----------|
| ESPNowBridge.cpp/h | ~200 riviä | - |
| CMPS14Application.cpp | - | ~20 riviä |
| secrets.h | - | ~5 riviä |
| **Yhteensä kompassi** | **~200 riviä** | **~25 riviä** |

### 8.3 Jatkokehitysideat

1. **Auto-pairing:** Broadcast kanavan selvitys, ei kovakoodattua MAC:ia
2. **OTA-päivitykset:** Crow Panelin päivitys kompassin kautta
3. **Näytön teemoitus:** Päivä/yö -tila
4. **Dataloggaus:** Matkan tallennus Crow Panelin flash-muistiin
5. **Multi-display:** Useampi Crow Panel samaan kompassiin

### 8.4 Tunnetut rajoitukset

1. **Kanava-synkronointi:** Jos WiFi-kanava vaihtuu, ESP-NOW katkeaa
2. **Kantama:** ESP-NOW ~50m sisätiloissa, riippuu häiriöistä
3. **ST7701 ohjain:** Vaatii tarkan alustussekvenssin

---

**Dokumentin loppu**

*Tämä dokumentti on luotu Claude Code -sessiossa 2026-01-28*
