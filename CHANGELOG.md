# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

# Changelog

## [1.1.0] - 2026-01-18

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

#### Security Features
- Rate limiting on failed login attempts (2-second delay)
- Password strength validation (minimum 8 characters)
- Secure password storage in NVS (SHA256 hash only)
- Default password warning on LCD display at first boot
- Session token validation on every endpoint
- Automatic session cleanup for expired sessions

#### New HTTP Endpoints
- `GET /` Login page or redirect to config if authenticated
- `POST /login` Login handler with password parameter
- `GET /logout` Logout and session cleanup
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
- `sha256_hash()` Calculate SHA256 hash of password

**CMPS14Preferences**
- `saveWebPassword()` Save password hash to NVS
- `loadWebPasswordHash()` Load password hash from NVS
- `hasWebPassword()` Check if password is configured

#### Configuration
- New constant in `secrets.h`: `DEFAULT_WEB_PASSWORD`
- New NVS key: `web_pass` (stores SHA256 hash, 64 bytes)
- New session configuration constants in `WebUIManager.h`:
  - `MAX_SESSIONS = 3` (concurrent users)
  - `SESSION_TIMEOUT_MS = 21600000` (6 hours)

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
- New buttons added to main page:
  - **CHANGE PASSWORD**
  - **LOGOUT**
- JavaScript enhancement in `/config`:
  - Automatic redirect to `/` on HTTP 401 response
  - Session expiry detection in status update loop

#### Comments & documentation
- Updated `WebUIManager.h` class description
- Added authentication flow documentation
- Inline comments for security-critical code sections
- Corrected typos in README, CONTRIBUTING, and inline comments
- Updated ToDo in README

### Security

#### Implemented protections
- Password never stored in plaintext (SHA256 hash only)
- Session tokens cryptographically random (ESP32 TRNG)
- HttpOnly cookies prevent JavaScript access
- Rate limiting prevents brute-force attacks
- Session timeout limits unauthorized access window
- All endpoints protected by default

#### Known limitations
- HTTP only (no HTTPS) - suitable for private LAN only
- No CSRF protection - do not expose to internet
- No IP whitelisting - any LAN device can attempt login
- No login attempt logging - failed attempts only delayed

### Fixed
- Added a delay in the eternal while-loop in main program's `setup()`, which was missing in previous releases.

### Deprecated
- N/A

### Removed
- N/A

### Performance

#### Memory impact
- RAM: +250 bytes (session table + auth logic)
- Flash: +5 KB (HTML pages + auth code)
- NVS: +64 bytes (password hash)

**Total impact:** Minimal

#### Runtime impact
- Login: ~50 ms (SHA256 computation)
- Session validation: <1 ms (token comparison)
- Session cleanup: <5 ms (per cleanup cycle)

No noticeable performance degradation.

### Migration Notes

#### Upgrading from v1.0.x
1. Flash new firmware
2. ESP32 automatically sets default password on first boot
3. Login with `cmps14admin`
4. **Change password immediately via web UI**
5. All existing settings (calibration, offsets) preserved

### Developer Notes

#### New Dependencies
- `mbedtls/md.h` SHA256 (built-in, no install needed)
- `esp_random.h` Hardware RNG (built-in)

#### Code Structure
- Authentication logic isolated in `WebUIManager` private methods
- Session management uses fixed-size array (no dynamic allocation)
- NVS operations in `CMPS14Preferences` for consistency

#### Future Enhancements
- Consider HTTPS/TLS support
- Add login attempt logging
- Implement IP whitelisting option
- Add TOTP two-factor authentication

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

[1.1.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.1.0
[1.0.1]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.0.1
[1.0.0]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v1.0.0
[0.5.1]: https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway/releases/tag/v0.5.1
