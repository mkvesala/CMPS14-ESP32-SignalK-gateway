# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - 2026-05-19

### Added

#### WiFi AP security and intrusion detection
- `WiFi.softAP()` called immediately after `WiFi.mode(WIFI_AP_STA)` — AP interface secured before any client can connect
  - Hidden SSID (`ssid_hidden=1`) — network not advertised
  - WPA2 password (`AP_PASS` from `secrets.h`, min 8 characters)
  - Maximum 1 concurrent connection
- AP intrusion detection with three-layer defence:
  - **Layer 1 — Hidden SSID**: network not visible to scanners
  - **Layer 2 — WPA2 password**: connection blocked without `AP_PASS`
  - **Layer 3 — Immediate deauth + alert**: if a station connects despite layers 1–2, it is kicked instantly and the MAC address is logged
- `WiFi.onEvent(ARDUINO_EVENT_WIFI_AP_STACONNECTED)` registered in `begin()` before `WiFi.begin()` so no connection event is missed
  - Callback runs in FreeRTOS `arduino_events` task: calls `esp_wifi_deauth_sta()` immediately (thread-safe ESP-IDF call)
  - Copies sender MAC before setting `volatile bool ap_intruder` flag — `loop()` always reads a complete address
- New `handleAPIntruder()` called from `loop()` immediately after `handleWifi()`
  - Clears the flag before display call so a rapid second event is not lost
  - Shows alert on LCD: `AP: INTRUDER!` / MAC

#### WebSocket watchdog
- New `handleWatchdog()` called from `loop()` immediately after `handleWebsocket()`
- Detects the failure mode where WiFi layer-2 association is alive (`WiFi.isConnected()` returns `true`, RSSI valid) but the ESP32 TCP/IP stack (lwIP) is silently dead — a state that `handleWifi()` reconnect logic cannot detect because it only checks layer-2
- Root cause: when the host running SignalK server goes to sleep, TCP connections are severed but the 802.11 association remains; on wake-up the lwIP stack may not recover, leaving `ping`, `WebServer` and ESP-NOW unresponsive while the Arduino loop continues normally
- Watchdog triggers `ESP.restart()` when: `wifi_state == CONNECTED` AND WebSocket has not been open for `WS_WATCHDOG_MS` (~10 min) AND a prior successful WebSocket session exists
  - The `last_ws_activity_ms == 0` guard prevents restart loops when a SignalK server is never reachable — watchdog only arms after first successful connection
  - LCD shows `WATCHDOG` / `RESTARTING...` for ~2 s before restart
- New timing constant: `WS_WATCHDOG_MS = 599983UL` (~10 min, prime to avoid harmonic collisions)
- New timer variable: `last_ws_activity_ms` (updated in `handleWebsocket()` whenever `signalk.isOpen()` is true)

#### Loop runtime watchdog
- `handleWatchdog()` extended with a loop runtime check independent of WiFi state
- Detects the failure mode where `ws.connect()` blocks the Arduino loop for the TCP connection timeout (~20 s) on each reconnect attempt — the loop keeps running but at a fraction of normal speed, causing the reconnect exponential backoff to collapse to minimum interval
- Root cause: `ArduinoWebsockets::connect()` is a synchronous blocking call; when the SignalK server host goes to sleep (e.g. laptop lid closed), TCP SYN packets get no response and the call blocks until the lwIP TCP timeout fires
- Loop runtime watchdog triggers `ESP.restart()` when: loop EMA `loop_avg_us > LOOP_WATCHDOG_US` (~100 ms) AND loop monitoring has been initialised (`monitoring == true`)
  - EMA α = 0.01: a single 20 s blocking call pushes EMA to ~200 000 µs, well above the 99 991 µs threshold
  - Guard `monitoring == false` on first boot prevents false trigger before EMA has been initialised
  - LCD shows `LOOP WATCHDOG` / `RESTARTING...` for ~2 s before restart
  - After restart, exponential WebSocket reconnect begins from minimum interval (`WS_RETRY_MS`)
- New constant: `LOOP_WATCHDOG_US = 99991UL` (~100 ms, prime to avoid harmonic collisions)

#### WiFi connection monitoring
- RSSI displayed on LCD every ~90 s while WiFi is connected (`WIFI RSSI` / `-xx dBm`)
- Reconnect counter shown on LCD when connection is lost (`WIFI LOST` / `RECONNECT #N`)
- `display.setWifiInfo()` called every 503 ms in `CONNECTED` state — keeps WebUI RSSI and IP address current between reconnects

#### Hardened WiFi reconnect
- `handleWifi()` `CONNECTED` state: replaced the plain `WiFi.disconnect(); WiFi.begin(...)` reconnect with a full STA teardown sequence:
  - `signalk.closeWebsocket()` — closes any stale WebSocket before tearing down WiFi (safe even if the TCP connection is already dead)
  - `WiFi.disconnect(true)` — full STA interface teardown (`wifioff=true`); the AP interface and AP-based intrusion detection are unaffected because `WiFi.begin()` later restores `AP_STA` mode
  - `delay(200)` — lets the radio settle before restarting the connection attempt
  - `WiFi.setSleep(false)` — reapplied, since STA teardown resets WiFi modem-sleep state
  - `applyStaticIP()` — reapplied, since static IP configuration does not survive STA teardown
- Root cause: the previous reconnect did not always recover the connection — observed when macOS power-saving stalled the SignalK server's network stack, leaving the ESP32 stuck until manually rebooted. The stale SignalK WebSocket also remained marked open (`ws_open == true`) until `handleWebsocket()` later detected the closure
- `handleWifi()` `CONNECTING → CONNECTED` transition: added `next_ws_try_ms = now` alongside the existing `expn_retry_ms = WS_RETRY_MS` reset — prevents a stale pre-outage backoff timestamp from delaying the SignalK WebSocket reconnect by up to `WS_RETRY_MAX_MS` (~120 s) after WiFi recovers
- Removed the dead `wifi_state = WifiState::DISCONNECTED` assignment in the `CONNECTED`-state reconnect branch — it was always immediately overwritten by `WifiState::CONNECTING` before any display update

#### Static IP configuration
- New `applyStaticIP()` method in `CMPS14Application`, called from `begin()` (before `WiFi.begin()`) and from the hardened reconnect sequence above
- Applies `WIFI_STATIC_IP`, `WIFI_GATEWAY`, `WIFI_SUBNET` from `secrets.h` via `WiFi.config()` — the gateway address also doubles as the DNS server
- On `WiFi.config()` failure, displays `STATIC IP` / `CONFIG FAILED` on LCD and falls back to DHCP (connection attempt proceeds regardless)
- New `secrets.h` / `secrets.example.h` constants: `WIFI_STATIC_IP`, `WIFI_GATEWAY`, `WIFI_SUBNET` — always applied (no enable/disable flag); choose `WIFI_STATIC_IP` outside the router's DHCP pool to avoid conflicts

#### Persistent attitude leveling
- Pitch/roll leveling (`pitch_level` / `roll_level`) now persists to NVS and is restored automatically at boot, alongside the other persisted settings
- Root cause: leveling captured by the WebUI `/level` endpoint lived only in RAM. Any restart — in particular a watchdog-triggered `ESP.restart()` (see WebSocket / loop runtime watchdogs above) — silently discarded the leveling, returning attitude output to its un-zeroed state without any user-visible indication
- New `CMPS14Preferences::saveLevel(float pitch_level, float roll_level)` writes NVS keys `pitch_lvl` / `roll_lvl` in a single `begin/end`; both restored in `load()` (default `0.0` when absent)
- New setters `CMPS14Processor::setPitchLevel()` / `setRollLevel()` apply the loaded values back onto the compass
- `WebUIManager::handleLevel()` persists the captured leveling immediately after `compass.level()`
- `CMPS14Processor::reset()` now also clears `pitch_level` / `roll_level` (the WebUI RESET resets the sensor registers, so the stored leveling is reset with them); `WebUIManager::handleReset()` persists the cleared `0,0` to NVS. RESET is the only action that zeroes leveling — a normal restart preserves it

#### New files / includes
- `#include <esp_wifi.h>` added to `CMPS14Application.h` — provides `esp_wifi_deauth_sta()`

### Fixed

- **WiFi reconnect — `initWifiServices()` called multiple times**: added `wifi_services_initialized` guard flag to `CMPS14Application`. `ArduinoOTA.begin()` and `webui.begin()` (including `setupRoutes()`) are now called only once for the lifetime of the application. Previously, each WiFi reconnect triggered a full re-initialisation: `ArduinoOTA.begin()` re-registered mDNS and re-bound UDP port 3232 without releasing the previous socket, and `setupRoutes()` appended duplicate route entries to the WebServer's internal linked list — both leaking memory on every reconnect. WebSocket reconnect is unaffected; it is handled as before by `handleWebsocket()`.

- **Pitch and roll not updating in SignalK at heading rate**: previously pitch and roll were included in the SignalK heading delta only when they individually exceeded a 0.05° deadband. Because the CMPS14 BNO055 fusion algorithm produces stable attitude output while the EMA-smoothed heading fluctuates continuously, pitch and roll updated far less frequently than heading on the SignalK server. Pitch and roll are now included unconditionally in every delta that the heading deadband triggers — they update at the same rate as heading (~100 ms). The heading deadband is unchanged.

- **WebSocket reconnect exponential backoff reset on brief connection**: `handleWebsocket()` previously used two sequential `if` blocks — the first attempted `connectWebsocket()`, the second checked `signalk.isOpen()`. If `ws.connect()` returned `true` (connection momentarily established), the second block immediately reset `expn_retry_ms` back to `WS_RETRY_MS` and `last_ws_activity_ms` to `now` in the same iteration — before `ws.poll()` had a chance to detect the `ConnectionClosed` event. The result was a hard ~2 s reconnect cycle with no exponential growth, and the network watchdog timer perpetually reset. Fixed by restructuring into `if (isOpen()) { ... } else { reconnect logic }` so that `expn_retry_ms` and `last_ws_activity_ms` are only updated when the connection was already confirmed open at the start of the iteration.

### Removed

- **Pitch and roll min/max tracking and SignalK publishing**: `MinMaxDelta` struct, `CMPS14Processor::updateMinMaxDelta()`, `CMPS14Processor::getMinMaxDelta()`, `SignalKBroker::sendPitchRollMinMaxDelta()`, and associated timing constants removed entirely. The feature was non-functional: the SignalK paths used (`navigation.attitude.pitchMin/Max`, `navigation.attitude.rollMin/Max`) conflict with the existing `navigation.attitude.pitch/roll` leaf nodes in the SignalK server data tree, causing the server to silently reject the deltas. Sufficient min/max recording is available through SignalK-native clients and dashboards.

- **ESP-NOW attitude leveling command**: `ESPNowBroker::processLevelCommand()` and its response sending removed entirely. `ESPNow::LevelCommand`, `ESPNow::LevelResponse` structs and `LEVEL_COMMAND = 10`, `LEVEL_RESPONSE = 11` enum values removed from `espnow_protocol.h`. The companion display project (ESP32-Crowpanel-compass v2.0.0) has been updated to a pure ESP-NOW listener role — it no longer sends level commands or expects responses. Attitude leveling remains fully functional via the WebUI `/level` endpoint.

### Changed

- `WIFI_TIMEOUT_MS` updated from `90001` to `179999` (~3 minutes)

### Developer Notes

#### AP security — three lines of defence

The AP interface is required for ESP-NOW coexistence (`WIFI_AP_STA` mode) and is not intended for external client connections.

| Line | Mechanism | Where in code |
|---|---|---|
| **1. Hidden SSID** | `ssid_hidden=1` — network not advertised | `WiFi.softAP(..., 1, 1, 1)` |
| **2. WPA2 password** | `AP_PASS` (min 8 chars) | `secrets.h: AP_PASS` |
| **3. Immediate deauth + alert** | Kicked instantly, MAC shown on LCD | `WiFi.onEvent(...)` + `handleAPIntruder()` |

New private members in `CMPS14Application`:
```cpp
volatile bool ap_intruder        = false;    // written in callback, read in loop()
uint8_t       ap_intruder_mac[6] = {};       // copied atomically before flag is set
```

## [1.3.1] - 2026-04-06

### Changed

Patching documentation only. Updated README to use shared UML class diagram (master now in ESP32-Crowpanel-compass repository).

## [1.3.0] - 2026-03-02

### Changed

#### ESP-NOW protocol update
- New shared protocol header `espnow_protocol.h` defining a structured packet format for all ESP-NOW communication
  - `ESPNow` namespace with `ESPNowHeader` (8-byte fixed header), payload structs, `ESPNowPacket<T>` wrapper template, and `initHeader()` helper
  - Magic number `0x45534E57` (`ESNW`) identifies packets from our network
  - `ESPNowMsgType` enum for message routing (`HEADING_DELTA`, `BATTERY_DELTA`, `WEATHER_DELTA`, `LEVEL_COMMAND`, `LEVEL_RESPONSE`)
- `ESPNowBroker` updated to use the new protocol structs instead of raw byte arrays
  - `sendHeadingDelta()` now wraps `HeadingDelta` in `ESPNowPacket<HeadingDelta>` with proper header
  - `processLevelCommand()` now sends `ESPNowPacket<LevelResponse>` instead of manually assembled byte array
  - `onDataRecv()` now validates incoming packets by header magic, message type, and payload length instead of raw byte matching

#### New files
- `espnow_protocol.h` — shared ESP-NOW protocol definitions (header, payloads, packet wrapper, helpers)

### Developer Notes

#### ESP-NOW packet format (v1.3.0)
All ESP-NOW packets now use a common envelope: 8-byte header + typed payload.

Header:
```cpp
struct ESPNowHeader {
    uint32_t magic;           // 0x45534E57 ('ESNW')
    uint8_t  msg_type;        // ESPNowMsgType enum
    uint8_t  payload_len;     // payload size in bytes
    uint8_t  reserved[2];     // padding (zero)
} __attribute__((packed));
```

Payload structs defined for: `HeadingDelta`, `BatteryDelta`, `WeatherDelta`, `LevelCommand`, `LevelResponse`.

Receivers should:
1. Check `len >= sizeof(ESPNowHeader)`
2. Validate `magic == ESPNOW_MAGIC`
3. Check `len >= sizeof(ESPNowHeader) + payload_len`
4. Route on `msg_type`

#### Breaking change
ESP-NOW packets are no longer raw structs — receivers from v1.2.0 must be updated to parse the new header+payload format.

## [1.2.0] - 2026-02-11

### Added

#### ESP-NOW support
- New `ESPNowBroker` class for broadcasting compass data to and executing commands received from ESP-NOW peers
  - Broadcasts `HeadingDelta` struct (heading, heading_true, pitch, roll in radians)
  - ~20 Hz broadcast rate (53 ms interval)
  - Deadband filtering (0.25°) to reduce unnecessary transmissions
  - Broadcast mode (FF:FF:FF:FF:FF:FF) - any ESP-NOW receiver can listen
  - Executes `compass.level()` when receives attitude leveling command from an ESP-NOW peer
  - Confirms the leveling to the peer via ESP-NOW

#### New files
- `ESPNowBroker.h` - ESP-NOW broker class declaration
- `ESPNowBroker.cpp` - ESP-NOW broker implementation

#### New methods

**ESPNowBroker**
- `begin()` Initialize ESP-NOW in broadcast mode
- `sendHeadingDelta()` Broadcast compass data to all listeners
- `processLevelCommand()` Execute and confirm attitude leveling command

#### Refactored
- `computeAngDiffRad()` moved from `SignalKBroker` (private method) to `harmonic.cpp` (global function)
  - Now shared between `SignalKBroker` and `ESPNowBroker`
  - Declared in `harmonic.h`

### Changed

#### WiFi mode
- Changed from `WIFI_STA` to `WIFI_AP_STA` to enable ESP-NOW alongside WiFi
  - ESP-NOW requires AP mode to function while WiFi STA is connected
  - No impact on existing WiFi/SignalK functionality

#### Architecture
- `CMPS14Application` now owns `ESPNowBroker` instance ("the espnow")
- New `handleESPNow()` method in application loop
- New timing constant `ESPNOW_TX_INTERVAL_MS = 53`

### Deprecated
- `DisplayManager`:
  - `void setWifiInfo(int32_t rssi, uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3)` use `void setWifiInfo(int32_t rssi, uint32_t ip)`instead
  - Usage:
  ```cpp
  int32_t rssi = WiFi.RSSI();
  uint32_t ip = (uint32_t)WiFi.localIP();
  display.setWifiInfo(rssi, ip);
  ```

### Performance

- ESP-NOW broadcast adds minimal overhead (~16 bytes per transmission)
- Deadband filtering prevents unnecessary broadcasts when compass is stationary

### Developer Notes

#### ESP-NOW data format
Broadcast packet is `CMPS14Processor::HeadingDelta` struct (16 bytes):
```cpp
struct HeadingDelta {
    float heading_rad;      // Magnetic heading (radians)
    float heading_true_rad; // True heading (radians)
    float pitch_rad;        // Pitch (radians)
    float roll_rad;         // Roll (radians)
};
```

#### ESP-NOW attitude leveling command format
Command from ESP-NOW peer (broadcast):
```cpp
struct LevelCommand {
  uint8_t magic[4];     // "LVLC"
  uint8_t reserved[4];  // future use
};
```
Confirm leveling to ESP-NOW peer (unicast to peer MAC):
```cpp
struct LevelResponse {
  uint8_t magic[4];     // "LVLR"
  uint8_t success;      // 1 = ok, 0 = failed
  uint8_t reserved[3];  // future use
};
```

#### Receiving ESP-NOW broadcasts
Any ESP32 device can receive broadcasts by:
1. Calling `esp_now_init()`
2. Registering receive callback with `esp_now_register_recv_cb()`
3. Parsing incoming 16-byte `HeadingDelta` struct

## [1.1.0] - 2026-01-24

### Added

#### Web UI authentication
- Session-based authentication for web configuration interface
  - SHA256 password hashing with mbedtls
  - Hardware random session token generation (128-bit)
  - HttpOnly session cookies
  - 6-hour session timeout
  - Support for up to 3 concurrent authenticated sessions
  - Least Recently Used (LRU) session replacement

#### New Web UI pages
- **Login page (`/`)**
  - Password input
  - Automatic redirect to `/config` if already authenticated
- **Change password page (`/changepassword`)**
  - Current password verification
  - New password validation (minimum 8 characters)
  - Password confirmation field
- **Logout functionality (`/logout`)**
  - Session cleanup on server and browser
  - Automatic redirect to login page

#### Security
- Login throttling of failed login attempts
- Password lenght min 8 characters validation
- Password storage in NVS as SHA256 hash
- Default password usage warning on LCD display
- Session token validation on endpoints
- Automatic session cleanup for expired sessions

#### New HTTP Endpoints
- `GET /` Login page or redirect to `/config` if authenticated
- `POST /login` Login handler with password parameter
- `POST /logout` Logout and session cleanup
- `GET /changepassword` Password change form
- `POST /changepassword` Password change handler
- `GET /config` Main configuration page (renamed from `/`)

#### New methods

**WebUIManager**
- `handleLogin()` Process login attempts
- `handleLoginPage()` Render login page HTML
- `handleLogout()` Clear session and redirect
- `handleChangePassword()` Process password change
- `handleChangePasswordPage()` Render password change form
- `requireAuth()` Check authentication and send 401 if unauthorized
- `isAuthenticated()` Check authentication without HTTP response
- `createSession()` Generate new session token
- `validateSession()` Validate session token and update `last_seen`
- `cleanExpiredSessions()` Remove expired sessions from memory
- `sha256Hash()` Calculate SHA256 hash of password
- `parseSessionToken()` Extract session token from cookie header
- `checkLoginRateLimit()` Check if IP address is subject to rate limiting
- `recordFailedLogin()` Record failed login attempt for IP address
- `recordSuccessfulLogin()` Clear login attempt tracking for IP address
- `cleanOldLoginAttempts()` Remove outdated login attempts from tracking

**CMPS14Preferences**
- `saveWebPassword()` Save password hash to NVS
- `loadWebPasswordHash()` Load password hash from NVS

#### Configuration
- New constant in `secrets.h`: `DEFAULT_WEB_PASSWORD`
- New NVS key: `web_pass` (stores SHA256 hash, 64 bytes)
- New session configuration constants in `WebUIManager.h`:
  - `MAX_SESSIONS = 3` (concurrent users)
  - `SESSION_TIMEOUT_MS = 21600000` (6 hours)
- New static array `HEADER_KEYS` to be used in `WebUIManager::begin()` by `server.collectHeaders(..)`
- New login throttling constants in `WebUIManager.h`:
  - `MAX_LOGIN_ATTEMPTS = 5` (logins per IP address)
  - `MAX_IP_FOLLOWUP = 5` (IP addresses to be tracked simultaneously)
  - `THROTTLE_WINDOW_MS = 60000` (login attempts window 1 min)
  - `LOCKOUT_DURATION_MS = 300000` (lockout 5 mins)
- New structs to store sessions and login attempts

### Changed

#### Endpoint protection
- All configuration endpoints now require authentication:
  - `/status` Returns 401 if unauthorized
  - `/cal/on`, `/cal/off`, `/store/on`, `/reset/on` Calibration control
  - `/offset/set`, `/dev8/set`, `/magvar/set` Settings endpoints
  - `/heading/mode`, `/calmode/set` Mode configuration
  - `/restart`, `/level` System operations
  - `/deviationdetails` Deviation table and curve

#### UI updates
- Main configuration page moved from `/` to `/config`
- *LEVEL CMPS14* button has been replaced above status block and renamed to *LEVEL ATTITUDE*
- *RESTART ESP32* button has been renamed to *RESTART*
- New buttons added to main page:
  - *CHANGE PASSWORD*
  - *LOGOUT*
- JavaScript enhancement:
  - Automatic redirect to `/` on HTTP 401 response
  - Session expiry detection in status update loop
- CSS definitions have been updated for better responsiveness

#### HTTP method updates
- Changed state-modifying endpoints from GET to POST:
  - `/cal/on`, `/cal/off`, `/store/on`, `/reset/on` (GET → POST)
  - `/offset/set`, `/dev8/set`, `/magvar/set` (GET → POST)
  - `/calmode/set`, `/heading/mode` (GET → POST)
  - `/restart`, `/level` (GET → POST)
- Parameters now sent in POST body instead of URL query strings
- HTML forms in updated to use POST method

### Security notes

#### Implemented protections
- Password stored as SHA256 hash in NVS
- Session tokens cryptographically random (ESP32 TRNG)
- HttpOnly cookies
- Login throttling
- Session timeout
- Endpoints authentication

#### Login throttling
- Light weight IP-based login attempt tracking
- 5-minute lockout after 5 failed login attempts per IP address in 1 min
- Tracks up to 5 IP addresses simultaneously
- Automatic cleanup of old login attempts (1-minute window)

#### Known limitations
- HTTP only (no HTTPS) - suitable for private LAN only
- No true CSRF protection - do not expose to internet
- No IP whitelisting - any LAN device can attempt login
- No login attempt logging - failed attempts only locked out for 5 mins

### Fixed
- Added a delay in the eternal while-loop in main program's `setup()`, which was missing in previous releases.
- CSS definitions in `<style>` have been updated, syntax errors in previous releases.

### Deprecated
- N/A

### Removed
- N/A

### Performance

No noticeable performance degradation.

### Migration Notes

#### Upgrading from v1.0.x
1. Flash new firmware
2. ESP32 automatically sets default password on first boot based on `secrets.h` constant
3. Login with default password
4. **Change password immediately via web UI**
5. All existing settings (calibration, offsets) preserved

### Developer Notes

#### New Dependencies
- `mbedtls/md.h` SHA256 (built-in, no install needed)
- `esp_random.h` Hardware RNG (built-in)

#### Code Structure
- Authentication logic isolated in `WebUIManager` private methods
- NVS operations in `CMPS14Preferences` for consistency

#### External integrations
If you call the web endpoints:
1. Update HTTP method from GET to POST for state-changing endpoints
2. Move URL parameters from query strings to POST body
3. Example: `GET /level` → `POST /level` (no parameters)
4. Example: `GET /offset/set?v=10` → `POST /offset/set` with body `v=10`
5. Read-only endpoints (GET `/status`, GET `/deviationdetails`) remain unchanged

## [1.0.1] - 2026-01-09

### Added
- Dedicated OTA_PASS, set in secrets.(example).h and used in CMPS14Application.cpp::initWifiServices()

### Changed
- In SignalKBroker::sendHdgPitchRollDelta() and SignalKBroker::sendPitchRollMinMaxDelta() the delta will be requested from the compass only after ws_open has been checked

## [1.0.0] - 2026-01-02

### Added
- Object-oriented refactored architecture
  - CMPS14Sensor for communicating with the actual sensor
  - CMPS14Processor for main compass logic
  - CMPS14Preferences for persistent storage in ESP32 NVS
  - SignalKBroker for websocket integration to SignalK server
  - DisplayManager for handling LCD and LEDs
  - CMPS14Application to provide the app orchestrating everything
  - DeviationLookup for lookup table for deviation values
- Added attitude leveling feature
- Performance monitoring (heap, loop runtime, stack)
- Performance data, SW/FW versions, system uptime to web ui status block
- Message queue (fifo) for LCD message buffering
- New led states for connection statuses

### Changed
- Complete rewrite from procedural v0.5.x to OOP v1.0.0
- Performance optimization during rewrite

## [0.5.1] - 2025-11-24 (legacy/procedural-0.5.x)

### Added
- Initial procedural implementation

[2.0.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v2.0.0
[1.3.1]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.3.1
[1.3.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.3.0
[1.2.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.2.0
[1.1.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.1.0
[1.0.1]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.0.1
[1.0.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.0.0
[0.5.1]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v0.5.1
