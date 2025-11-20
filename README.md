# CMPS14-ESP32-SignalK Gateway

ESP32-based reader for Robot Electronics [CMPS14](https://www.robot-electronics.co.uk/files/cmps14.pdf) compass & attitude sensor. Sends heading, pitch and roll (including min and max) to [SignalK](https://signalk.org) server via websocket/json.

Applies installation offset, deviation and magnetic variation to raw angle to determine compass heading, magnetic heading and optionally true heading. Uses harmonic model to compute deviation at any given compass heading, based on user-measured deviations at 8 cardinal and intercardinal directions. Subscribes magnetic variation from SignalK server. This is prioritized over manually entered variation to determine true heading.

Uses LCD 16x2 to show status messages and heading. If no wifi around, runs on LCD only mode.

Runs a webserver to provide configuration web UI. Configurable parameters: calibration mode (full auto, auto, manual), installation offset, measured deviations, manual variation and heading mode (true, magnetic).

OTA updates and persistent storage of configuration in ESP32 Preferences are enabled.

Led indicators for calibration mode and connection status (two leds).

## Features

### Compass and attitude

1. Reads angle, pitch and roll from CMPS14 at ~20 Hz cycle
2. Applies installation offset (user input) to raw angle for compass heading
3. Applies smoothing to compass heading
4. Computes magnetic heading using harmonic deviation model on compass heading
5. Computes true heading (optional), using either
   - Live *navigation.magneticVariation* received from SignalK (prioritized over user input)
   - Manual variation from user input on web UI (used automatically whenever *navigation.magneticVariation* is not available)
6. Installation offset and selected heading mode are stored persistently in ESP32 Preferences

### Deviation

1. Takes 8 user-measured deviations (N, NE, E, SE, S, SW, W, NW) as input from web UI
2. Fits 5-parameter sinusoidal model:
   ```
   dev(hdg) = A + Bsin(hdg) + Ccos(hdg) + Dsin(2hdg) + Ecos(2hdg)
   ```
4. User-measured deviations and computed 5 coeffs are stored persistently in ESP32 Preferences
5. Full deviation curve and deviation table available on web UI
   - Deviation curve as SVG graph
   - Deviation table with 10° steps

**Note that deviation can be applied only to a permanently mounted stable compass. While CMPS14 can be securely mounted to the vessel, it's behavior may still be altered by calibration (automatic or manual). It is recommended to keep deviation at 0° until there are undeniable evidence that the compass is stable and operating without any needs for regular calibration. It's obvious that the deviations should always be re-measured and computed after each calibration.**

### Magnetic variation

1. Subscribes *navigation.magneticVariation* path from SignalK server at ~1 Hz cycles. This is treated as primary and the most trusted source of magnetic variation.
2. User may enter magnetic variation manually on the web UI. This is a backup and the value will be used automatically if variation is not available at SignalK path.
3. User-defined manual variation is persistently stored in ESP32 Preferences.
4. Magnetic variation is used for computing true heading on magnetic heading.

**Note that when the SignalK connection is open, the magnetic heading will always be sent to SignalK *navigation.headingMagnetic* path regardless of the active heading mode (true/magnetic). It is a standard practise to compute true heading on server side using SignalK [Derived Data](https://github.com/SignalK/signalk-derived-data) plugin or similar, or on other clients such as [OpenCPN](https://opencpn.org) using [WMM](https://www.ncei.noaa.gov/products/world-magnetic-model).**

### SignalK communication

Connects to:
```
ws://<server>:<port>/signalk/v1/stream?token=<optional>
```

**Sends** on maximum ~10 Hz frequency, in radians, with a deadband of 0.25°:

1. *navigation.headingMagnetic*
2. *navigation.attitude.pitch*
3. *navigation.attitude.roll*
4. (optionally) *navigation.headingTrue*

**Sends** on maximum ~1 Hz frequency, in radians, only if changed:

1. *navigation.attitude.pitch.max*
2. *navigation.attitude.pitch.min*
3. *navigation.attitude.roll.max*
4. *navigation.attitude.roll.min*

The min and max values reset to zero in ESP32 restart and they are *not* persistently stored in ESP32 Preferences.

**Receives** on ~1 Hz frequency, in radians:

1. *navigation.magneticVariation* (if available at SignalK)

### Calibration modes

Three supported calibration modes and use-mode for normal operation:

1. *FULL AUTO* - CMPS14's built-in autocalibration & autosave + optional user configurable stop timer / manual stop
2. *AUTO* - auto-detect good enough calibration to save automatically
3. *MANUAL* - user-triggered calibration with manual save/replace
4. In *USE* mode the CMPS14 operates normally based on saved calibration profile (no calibration is running).

The desired calibration mode (including optional *FULL AUTO* timeout) is stored persistently in ESP32 Preferences.

When calibration is running, *SYS*, *ACC* and *MAG* indicators are monitored at ~2 Hz cycles. In *AUTO* mode the calibration profile will be saved when all three indicators equal 3 (best) over three consecutive cycles. In *MANUAL* mode user may decide when to save by monitoring the values changing on web UI status block. The calibration profile is saved on CMPS14 itself, *not* in ESP32 Preferences.

**Note that the *GYR* indicator for gyro is not monitored. There is a reported firmware bug in CMPS14 that makes *GYR* indicator unreliable.**

### Web UI (ESP32 Webserver)

Webserver provides simple HTML/JS user interface for user to configure:

1. Calibration mode on boot and optional *FULL AUTO* timeout (mins)
   - 0 mins timeout never turns *FULL AUTO* mode off automatically
   - Effective after restart
3. Installation offset (degrees)
   - Positive offset corrects the heading *towards* starboard, meaning that the compass mounting is tilted on port side
   - Negative offset corrects the heading *towards* port side, meaning that the compass mounting is tilted on starboard
   - Effective immediately
4. Measured deviations (degrees) at 8 cardinal and intercardinal directions
   - Assumes the user to measure deviations with normal routine of
     ```
     HDG (C) | Dev | HDG (M) | Var | HDG (T)
     ```
   - Effective immediately
5. Manual magnetic variation (degrees)
   - Positive value is E, negative value is W
   - Effective immediately, however SignalK *navigation.magneticVariation* will be used instead whenever available
6. Heading mode (true/magnetic)
   - True is selected by default
   - Magnetic heading will always be sent to SignalK *navigation.headingMagnetic* path, also when True is selected
   - Effective immediately
  
All above are stored persistently in ESP32 Preferences and will automatically be retrieved on boot.

Additionally the user may:

1. Start the calibration in *MANUAL* mode
2. Stop the calibration in all calibration modes without saving calibration profile
3. Save the calibration profile in *AUTO* and *MANUAL* calibration modes
   - If calibration profile has been saved since boot, the *REPLACE* button is shown instead of *SAVE*
4. Reset CMPS14 to factory settings
   - There is a 600 ms delay after reset in the background, doubling the delay from data sheet recommendation
5. View the deviation curve and deviation table
   - Opens a new page with a back-button pointing to the configuration page
6. Restart ESP32
   - Opens a temporary page which will refresh back to the configuration page after 30 seconds
   - In the background, the restart will be executed ~5 seconds after pushing the button
   - Calls `ESP.restart()` of `esp_system`
7. View the parameters on status block
   - JS generated block that updates at ~1 Hz cycles
   - Shows: installation offset, compass heading, deviation on compass heading, magnetic heading, effective magnetic variation, true heading, pitch, roll, 3 calibration status indicators, 5 coeffs of harmonic model, IP address and wifi signal level description

### Webserver endpoints

```
Path                  Description               Parameters
----                  -----------               ----------
/                     Main UI                   none
/cal/on               Start calibration         none
/cal/off              Stop calibration          none
/store/on             Save calibration profile  none
/reset/on             Reset CMPS14              none
/calmode/set          Save calibration mode     ?calmode=<full|semi|use>&fastop=<0-60>
/offset/set           Installation offset       ?v=<-180-180>
/dev8/set             Eight deviation points    ?N=<n>&NE=<n>&E=<n>&SE=<n>&S=<n>&SW=<n>&W=<n>&NW=<n>
/deviationdetails     Deviation curve and table none
/magvar/set           Manual variation          ?v=<-180-180>
/heading/mode         Heading mode              ?mode=<true|mag>
/status               Status block              none
/restart              Restart ESP32             ?ms=5003
```

### LCD 16x2

1. Shows heading (true or magnetic) at ~1 Hz refresh
2. Shows messages of `setup()` on boot
3. Shows info messages on the way, related to calibration status, user interaction on web UI, OTA updates etc.
  - Info messages are visible for ~1 second before refreshed and replaced by heading again
4. To avoid unnecessary blinking the LCD will refresh only if the content to be shown is different from what's already on the display.

### Two led indicators

1. GPIO2 blue led indicator (built in led of SH-ESP32 board in my case)
   - Solid state: *USE* mode
   - Slow ~1 Hz blinking: *FULL AUTO* calibration mode active
   - Fast ~5 Hz blinking: *AUTO* or *MANUAL* calibration mode active
   - Off: Houston, we have a problem.
2. GPIO13 green led indicator (additional led I soldered onto the board)
   - Solid state: websocket connection to SignalK server is open
   - Fast ~5 Hz blinking: wifi is connected but websocket connection to SignalK server is down
   - Slow ~0.5 Hz blinking: no networking, running *LCD ONLY* mode
   - Off: I have a bad feeling about this.
  
## Project structure

```
/
- CMPS14-ESP32-SignalK-gateway.ino         // setup() and loop()
- globals.h / globals.cpp                  // constants and other globals
- cmps14.h / cmps14.cpp                    // read from and write commands to CMPS14
- signalk.h / signalk.cpp                  // websocket connectivity to SignalK and delta handling with json
- webui.h / webui.cpp                      // webserver to provide HTML/JS UI and handlers
- display.h / display.cpp                  // LCD 16x2 and led indicators
- harmonic.h / harmonic.cpp                // harmonic model to compute deviation
```

## Hardware used

1. ESP32 module (developed and tested with [SH-ESP32](https://docs.hatlabs.fi/sh-esp32/) board)
2. CMPS14 sensor (I2C mode) connected with 1.2 m CAT5 network cable
3. LCD 16x2 module(with I2C backpack) connected with 1.2 m CAT5 network cable
4. LEDs
   - blue led at GPIO2 (built in led of SH-ESP32)
   - green led at GPIO13 with 330 ohm resistor in series
5. Wifi AP for wireless LAN
6. Device in LAN running SignalK server
7. Sparkfun bi-directional [logic level converter](https://www.sparkfun.com/sparkfun-logic-level-converter-bi-directional.html)
   - SH-ESP32 runs 3.3 V internally
   - CMPS14 accepts both 3.3 V and 5 V
   - LCD accpets both 3.3 V and 5 V but is brighter with 5 V
   - Soldered logic level converter onto the board and used 5 V both for CMPS14 and LCD
8. Joy-IT step-down [voltage converter](https://joy-it.net/en/products/SBC-Buck04-5V)
   - SH-ESP32 accepts 8 - 32 V, this is a step-down to 5 V for CMPS14 and LCD
9. IP67 enclosures for CMPS14 and SH-ESP32
10. 3D printed [panel mount bezel](https://www.printables.com/model/158413-panel-mount-16x2-lcd-bezel) for LCD 16x2

## Software requirements

1. Arduino IDE 2.x or similar
2. ESP32 board package
3. Libraries:
   ```
   Arduino.h
   Wire.h
   WiFi.h
   WebServer.h
   ArduinoWebsockets.h
   ArduinoJson.h
   LiquidCrystal_I2C.h
   Preferences.h
   esp_system.h
   ArduinoOTA.h
   ```

## Installation

1. Clone the repo
   ```
   git clone https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway.git
   ```
2. Alternatively, download the code as zip
3. Set up your credentials in `secrets.h` (first by renaming the `secrets.example.h`)
   ```
   #define WIFI_SSID   "your_wifi"
   #define WIFI_PASS   "your_pass"
   #define SK_HOST     "ip or hostname of SignalK server"
   #define SK_PORT     3000 or whatever you have defined on SignalK server
   #define SK_TOKEN    "your_token"
   ```
5. Alternatively, use ESP32 built-in AP to configure credentials
6. Compile and upload
8. Open browser --> navigate to ESP32 ip-address for configuration page

Calibration procedure is documented on CMPS14 [datasheet](https://www.robot-electronics.co.uk/files/cmps14.pdf)

## Credits

Developed using:

- SH-ESP32 board
- ESP32 platform on Arduino IDE 2.3.6
- CMPS14 documentation
- SignalK specification

Inspired by [Magnetix - a digital compass with NMEA2000](https://open-boat-projects.org/en/magnetix-ein-digitaler-kompass-mit-nmea2000/).

Developed by Matti Vesala in collaboration with GhatGPT Plus 5.







