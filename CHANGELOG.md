# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0] - 2026-02-11

### Added

#### ESP-NOW support
- New `ESPNowBroker` class for broadcasting compass data to and executing commands received from ESP-NOW peers
  - Broadcasts `HeadingDelta` struct (heading, heading_true, pitch, roll in radians)
  - ~17 Hz broadcast rate (53 ms interval)
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

[1.2.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.2.0
[1.1.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.1.0
[1.0.1]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.0.1
[1.0.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.0.0
[0.5.1]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v0.5.1
