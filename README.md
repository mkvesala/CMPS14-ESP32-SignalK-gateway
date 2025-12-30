![Logo](projectlogo.svg)

# CMPS14-ESP32-SignalK Gateway

[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue)](https://www.espressif.com/en/sdks/esp-arduino)
[![Sensor: CMPS14](https://img.shields.io/badge/Sensor-CMPS14-lightgrey)](https://www.robot-electronics.co.uk/files/cmps14.pdf)
[![Server: SignalK](https://img.shields.io/badge/Server-SignalK-orange)](https://signalk.org)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

ESP32-based reader for Robot Electronics [CMPS14](https://www.robot-electronics.co.uk/files/cmps14.pdf) compass & attitude sensor. Sends heading, pitch and roll to [SignalK](https://signalk.org) server via websocket/json.

<img src="project5.jpeg" width="240">

Applies installation offset, deviation and magnetic variation to raw angle to determine compass heading, magnetic heading and optionally true heading. Computes deviation at any compass heading, based on user-measured deviations at 8 cardinal and intercardinal directions. Subscribes magnetic variation from SignalK server. This is prioritized over manually entered variation to determine true heading.

Uses LCD 16x2 to show status messages and heading. If no wifi around, runs on LCD only.

Runs a webserver to provide web UI for CMPS14 configuration. Configurable parameters: calibration mode (full auto, auto, manual), installation offset, measured deviations, manual variation, heading mode (true, magnetic) and attitude leveling.

OTA updates and persistent storage of configuration in ESP32 NVS are enabled.

Led indicators for calibration mode and connection status (two leds).

## Purpose of the project

1. Needed a reliable low-cost digital compass that could be connected to SignalK server of my vessel
2. To learn ESP32 capabilities for other digital boat projects that are on my backlog
3. To refresh my C/C++ skills as I had not delivered any code since 2005 (and before that mostly Java and Smallworld Magik)

Started the project Arduino-style by copying code from a previous project (VEDirect-ESP32-SignalK-gateway). Then, just kept playing around with Arduino. The next project will be most likely based on SensESP/PlatformIO to keep things less complicated.

## Release history

```
Release    Branch                     Comment

v1.0.0     main                       Latest release. Refactored into classes
                                      with new features not implemented in 0.5.x.
v0.5.1     legacy/procedural-0.5.x    Last fully procedural version.
```
## Classes

### Class CMPS14Sensor

The heart of the project is the modest librarish class `CMPS14Sensor` which communicates with the CMPS14 device. It has the following public API:

```
bool    begin(TwoWire &wirePort) 
bool    available()
bool    read(float &angle_deg, float &pitch_deg, float &roll_deg)
bool    sendCommand(uint8_t cmd)
uint8_t readRegister(uint8_t reg)
bool    isAck(uint8_t byte)
bool    isNack(uint8_t byte)
```
The simple usage of CMPS14Sensor could be:

```
#include <Arduino.h>
#include <Wire.h>
#include "CMPS14Sensor.h"

CMPS14Sensor sensor(0x60); // I2C address

void setup() {
   Serial.begin(115200);
   Wire.begin(16, 17);     // SDA, SCL
   delay(100);
   sensor.begin(Wire);
}

void loop() {
   float angle_deg, pitch_deg, roll_deg = 0.0f;
   if (sensor.available() && sensor.read(angle_deg, pitch_deg, roll_deg)) {
      Serial.println(angle_deg);
      Serial.println(pitch_deg);
      Serial.println(roll_deg);
   }
}

```
## Features

### Compass and attitude

1. Reads angle, pitch and roll from CMPS14 at ~20 Hz frequency
2. Applies installation offset (user input) to raw angle for compass heading
3. Applies smoothing to compass heading
4. Applies deviation on compass heading for magnetic heading
5. Computes true heading (optional), using either
   - Live *navigation.magneticVariation* received from SignalK (prioritized over user input)
   - Manual variation from user input on web UI (used automatically whenever *navigation.magneticVariation* is not available)
6. Applies leveling to pitch and roll
7. Installation offset and selected heading mode are stored persistently in ESP32 NVS, leveling of pitch and roll is not

### Deviation

1. Takes 8 user-measured deviations (N, NE, E, SE, S, SW, W, NW) as input from web UI
2. Computes 5 harmonic coefficients (A, B, C, D, E) that best fit the mathematical model `deviation(θ) = A + B·sin(θ) + C·cos(θ) + D·sin(2θ) + E·cos(2θ)` using least squares regression and Gauss-Jordan elimination, providing smooth sinusoidal curve through all 8 user-measured points
3. User-measured deviations and computed 5 coeffs are stored persistently in ESP32 NVS
4. A deviation lookup table is computed each time the 5 coeffs change and on ESP32 boot. The lookup table contains the deviation for each 1° over 360°. The lookup method will apply a linear interpolation for better accuracy when retrieving a value from the lookup table at a compass heading.
5. Deviation curve and deviation table at simplified 10° resolution available on web UI

**Note that deviation can be applied only to a permanently mounted stable compass. While CMPS14 can be securely mounted to the vessel, it's behavior may still be altered by calibration (automatic or manual). It is recommended to keep deviation at 0° until there are undeniable evidence that the compass is stable and operating without any needs for regular calibration. It's obvious that the deviations should always be re-measured and computed after each calibration.**

### Magnetic variation

1. Subscribes *navigation.magneticVariation* path from SignalK server at ~1 Hz cycles. This is treated as primary and the most trusted source of magnetic variation.
2. User may enter magnetic variation manually on the web UI. This is a backup and the value will be used automatically if variation is not available at SignalK path.
3. User-defined manual variation is persistently stored in ESP32 NVS.
4. Magnetic variation is used for computing true heading on magnetic heading.

**Note that when the SignalK connection is open, the magnetic heading will always be sent to SignalK *navigation.headingMagnetic* path regardless of the active heading mode (true/magnetic). It is a standard practise to compute true heading on server side using SignalK [Derived Data](https://github.com/SignalK/signalk-derived-data) plugin or similar, or on other clients such as [OpenCPN](https://opencpn.org) that utilize [WMM](https://www.ncei.noaa.gov/products/world-magnetic-model).**

### SignalK communication

Connects to:
```
ws://<server>:<port>/signalk/v1/stream?token=<optional>
```

**Sends** at maximum ~10 Hz frequency, in radians, with a deadband of 0.25°:

1. *navigation.headingMagnetic*
2. *navigation.attitude.pitch*
3. *navigation.attitude.roll*
4. (optionally) *navigation.headingTrue*

**Sends** at maximum ~1 Hz frequency, in radians, only if changed:

1. *navigation.attitude.pitch.max*
2. *navigation.attitude.pitch.min*
3. *navigation.attitude.roll.max*
4. *navigation.attitude.roll.min*

The min and max values reset to zero on ESP32 restart and after applying attitude leveling. They are *not* persistently stored in ESP32 NVS.

**Receives** at ~1 Hz frequency, in radians:

1. *navigation.magneticVariation* (if available at SignalK)

### Calibration modes

Three supported calibration modes and use-mode for normal operation:

1. *FULL AUTO* - CMPS14's built-in autocalibration & autosave + optional user configurable stop timer
2. *AUTO* - auto-detect good enough calibration to save automatically
3. *MANUAL* - user-triggered calibration with manual save/replace
4. In *USE* mode the CMPS14 operates normally based on saved calibration profile (no calibration is running).

When calibration is running, *SYS*, *ACC* and *MAG* indicators are monitored at ~2 Hz frequency.

*FULL AUTO* and *AUTO* will activate on ESP32 boot. The *FULL AUTO* will run until the user configured stop timer or eternally if the timer is set to 0. The *AUTO* will run until all three indicators equal 3 (the best) over three consecutive cycles, then the calibration profile will be saved and *USE* mode will be activated. When *USE/MANUAL* is selected, no calibration will be started on ESP32 boot. Instead, the device will start directly to *USE* mode. In *MANUAL* mode (press *CALIBRATE*) user may decide when to save by monitoring the values on web UI status block.

The calibration mode selection (including optional *FULL AUTO* timeout) is stored persistently in ESP32 NVS.

The calibration profile is saved on CMPS14 internally, *not* in ESP32 NVS.

**Note that CMPS14's *GYR* indicator for gyro is not monitored. There is a reported firmware bug in CMPS14 that makes *GYR* indicator unreliable.**

### Web UI (ESP32 Webserver)

<img src="ui1.jpeg" width="120"> <img src="ui2.jpeg" width="120"> <img src="ui3.jpeg" width="120"> <img src="ui4.jpeg" width="120">

Webserver provides simple responsive HTML/JS user interface for user to configure:

1. Calibration mode on boot and optional *FULL AUTO* timeout (mins)
   - 0 mins timeout never turns *FULL AUTO* mode off automatically
   - Effective after ESP32 boot
3. Installation offset (degrees)
   - Positive offset corrects the heading *towards* starboard, meaning that the compass mounting is tilted port side
   - Negative offset corrects the heading *towards* port side, meaning that the compass mounting is tilted starboard
   - Effective immediately
4. Measured deviations at 8 cardinal and intercardinal directions (degrees)
   - Assumes the user has measured deviations with the standard navigation routine:
     ```
     HDG (C) | Dev | HDG (M) | Var | HDG (T)
     ```
   - Effective immediately
5. Manual magnetic variation (degrees)
   - Positive value is E, negative value is W
   - Effective immediately, however SignalK *navigation.magneticVariation* will be used instead whenever available
6. Heading mode (true/magnetic)
   - True is the default
   - Magnetic heading will always be sent to SignalK *navigation.headingMagnetic* path, also when True is selected
   - Effective immediately
  
All above are stored persistently in ESP32 NVS and will be automatically retrieved on ESP32 boot.

Additionally the user may:

1. Start the calibration by pressing *CALIBRATE* in *MANUAL* mode
2. *STOP* the calibration in all calibration modes without saving calibration profile
3. *SAVE* the calibration profile in *AUTO* and *MANUAL* calibration modes
   - If calibration profile has already been saved since ESP32 boot, the *REPLACE* button is shown instead of *SAVE*
4. *RESET* CMPS14 to factory settings
   - There is a 600 ms delay after reset in the background, doubling the delay from data sheet recommendation
   - Reset does *not* reset configuration settings stored in NVS nor pitch/roll min/max values
5. *SHOW DEVIATION CURVE*
   - Opens a new page with a back-button pointing to the configuration page
   - Simplified deviation curve and deviation table presented 0...360° with 010° resolution
6. *LEVEL CMPS14* attitude to zero
   - Takes the negation of the latest pitch and roll to capture the leveling factors for attitude
   - Leveling factors are applied to the raw pitch and roll
   - Thus, user may reset the attitude to zero at any vessel position to start using proportional pitch and roll
   - Leveling is not incremental and the leveling factors are *not* stored persistently in ESP32 NVS
   - Leveling resets pitch/roll min/max values
8. *RESTART ESP32*
   - Opens a temporary page which will refresh back to the configuration page after 15 seconds
   - In the background, the restart will be executed ~5 seconds after pushing the button
   - Calls `ESP.restart()` of `esp_system`
9. View the parameters on status block
   - JS generated block that updates at ~1 Hz cycles
   - Shows: installation offset, compass heading, deviation on compass heading, magnetic heading, effective magnetic variation, true heading, pitch (leveling factor), roll (leveling factor), 3 calibration status indicators, 5 coeffs of harmonic model, debug heap memory status, debug loop task average runtime and free loop task stack memory, IP address and wifi signal level description, software version.

### Webserver endpoints

```
Path                Description               Parameters
----                -----------               ----------
/                   Main UI                   none
/cal/on             Start calibration         none
/cal/off            Stop calibration          none
/store/on           Save calibration profile  none
/reset/on           Reset CMPS14              none
/calmode/set        Save calibration mode     ?c=<0|1|2|3>&t=<0...60> // 0 = FULL AUTO, 1 = AUTO, 2 = MANUAL, 3 = USE
/offset/set         Installation offset       ?v=<-180...180> // Degrees (-) correct towards port side, (+) correct towards starboard
/dev8/set           Eight deviation points    ?N=<n>&NE=<n>&E=<n>&SE=<n>&S=<n>&SW=<n>&W=<n>&NW=<n> // <n> = deviation in degrees
/deviationdetails   Deviation curve and table none
/magvar/set         Manual variation          ?v=<-180...180> // Degrees (-) west, (+) east
/heading/mode       Heading mode              ?m=<1|0> // 1 = HDG(T), 0 = HDG(M)
/status             Status block              none
/restart            Restart ESP32             ?ms=5003 // Delay before actual restart in ms
/level              Level CMPS14 attitude     none
```
Endpoints can of course be used by any http get request. Thus, should one want to add leveling of attitude to, let's say, a [KIP](https://github.com/mxtommy/Kip) dashboard, just a simple webpage widget with a link to `http://<esp32ipaddress>/level` could be added next to pitch and roll gauges on the dashboard.

### LCD 16x2

1. Shows heading (true or magnetic, depending on user preference on web UI)
2. Shows info messages on the way, related to calibration status, user interaction on web UI, OTA update progress etc.
3. LCD shows messages from a message queue on fifo basis, every 1.5 seconds
4. To avoid unnecessary blinking the LCD will refresh only if the content to be shown is different from what's already on the display.

### Two led indicators

1. GPIO2 blue led indicator (built in led on my SH-ESP32 board)
   - Fast ~5 Hz blinking: wifi not connected
   - Medium ~2 Hz blinking: wifi connecting
   - Slow ~1 Hz blinking: wifi connected
   - Solid state: websocket connected to SignalK server
   - Off: I have a bad feeling about this.  
1. GPIO13 green led indicator (additional led soldered onto the board)   
   - Solid state: *USE* mode
   - Fast ~5 Hz blinking: *AUTO* or *MANUAL* calibration mode active
   - Slow ~0.5 Hz blinking: *FULL AUTO* calibration mode active
   - Off: Houston, we have a problem.
  
## Project structure

```
/
- CMPS14-ESP32-SignalK-gateway.ino                 // Owns CMPS14Application app, contains setup() and loop()
- version.h                                        // Software version
- CalMode.h                                        // Enum class for CMPS14 calibration modes
- WifiState.h                                      // Enum class for wifi states
- harmonic.h             | harmonic.cpp            // Struct and functions to compute deviations, class DeviationLookup
- CMPS14Sensor.h         | CMPS14Sensor.cpp        // Class CMPS14Sensor, the "sensor"
- CMPS14Processor.h      | CMPS14Processor.cpp     // Class CMPS14Processor, the "compass"
- CMPS14Preferences.h    | CMPS14Preferences.cpp   // Class CMPS14Preferences, the "compass_prefs"
- SignalKBroker.h        | SignalKBroker.cpp       // Class SignalKBroker, the "signalk"
- DisplayManager.h       | DisplayManager.cpp      // Class DisplayManager, the "display"
- WebUIManager.h         | WebUIManager.cpp        // Class WebUIManager, the "webui"
- CMPS14Application.h    | CMPS14Application.cpp   // Class CMPS14Application, the "app"
```

## Hardware used

1. ESP32 module (developed and tested with [SH-ESP32](https://docs.hatlabs.fi/sh-esp32/) board)
2. CMPS14 sensor (I2C mode) connected with 1.2 m CAT5E network cable
3. LCD 16x2 module(with I2C backpack) connected with 1.2 m CAT5E network cable
4. LEDs
   - blue led at GPIO2 (built-in led on SH-ESP32 board)
   - green led at GPIO13 in series with 330 ohm resistor
5. Sparkfun bi-directional [logic level converter](https://www.sparkfun.com/sparkfun-logic-level-converter-bi-directional.html)
   - SH-ESP32 runs 3.3 V internally
   - CMPS14 accepts both 3.3 V and 5 V
   - LCD accepts both 3.3 V and 5 V but is sharper with 5 V
   - Soldered logic level converter onto the board and used 5 V both for CMPS14 and LCD (I2C comms)
6. Joy-IT step-down [voltage converter](https://joy-it.net/en/products/SBC-Buck04-5V)
   - SH-ESP32 accepts 8 - 32 V, this is step-down to 5 V for CMPS14 and LCD
7. IP67 enclosures for CMPS14 and SH-ESP32, cable clands and SP13 connectors
8. Jumper wires, male row headers (2.54 mm)
9. 3D printed [panel mount bezel](https://www.printables.com/model/158413-panel-mount-16x2-lcd-bezel) for LCD 16x2 (temporarily a black 2 x 4 x 1 inch plastic box with a cut hole)
10. Wifi router providing wireless LAN AP
11. MacOS laptop in LAN running SignalK server

**No paid partnerships.**

## Software used

1. Arduino IDE 2.3.6
2. Espressif Systems esp32 board package 3.3.5
3. Additional libraries installed:
   - ArduinoWebsockets (by Gil Maimon version 0.5.4)
   - ArduinoJson(by Benoit Blanchon version 7.4.2)
   - ArduinoOTA (by Arduino, Juraj Andrassy version 1.1.0)
   - LiquidCrystal_I2C (by Frank de Brabander version 1.1.2)

## Installation

1. Clone the repo
   ```
   git clone https://github.com/mkvesala/CMPS14-ESP32-SignalK-gateway.git
   ```
2. Alternatively, download the code as zip
3. Set up your credentials in `secrets.h` (first by renaming the `secrets.example.h` to `secrets.h`)
   ```
   #define WIFI_SSID   "your_wifi"
   #define WIFI_PASS   "your_pass"
   #define SK_HOST     "ip or hostname of SignalK server"
   #define SK_PORT     3000 or whatever you have defined on SignalK server
   #define SK_TOKEN    "your_token"
   ```
4. **SECURITY: make sure that `secrets.h` is listed in your `.gitignore` file**
5. Connect CMPS14 and optionally LCD to the I2C pins of your ESP32 board
6. Connect and power up the ESP32 with the USB cable
7. Compile and upload with Arduino IDE (ESP tools and required libraries installed)
8. Open browser --> navigate to ESP32 webserver's ip-address for web UI (make sure you are in the same network with the ESP32)

Calibration procedure is documented on CMPS14 [datasheet](https://www.robot-electronics.co.uk/files/cmps14.pdf)

## Todo

- Replace the timers within `loop()` with separate tasks pinned to core 0 and 1 to improve performance
- Finish the hardware setup by soldering all wiring instead of using jumper wires and row headers

## Debug

Long term observations on release v1.0.0 running on SH-ESP32 board, LCD connected, wifi connected, SignalK server up/down randomly:

- Free heap memory (from `ESP.getFreeHeap()`): approximately 160 kB (60 %) free, total being 277 kB - does not vary that much
- Loop runtime exponential moving average (alpha 0.01): approximately 990 microseconds
- Loop task free stack (from `uxTaskGetStackHighWaterMark(NULL)`): stays below 4300 bytes

## Credits

Developed and tested using:

- SH-ESP32 board Rev 2.2.1
- Espressif Systems esp32 3.3.5 package on Arduino IDE 2.3.6
- SignalK server verion 2.18.0
- OpenCPN version 5.12.4-universal and KIP version 4.0.7 for visualization
- CMPS14 datasheet

Inspired by [Magnetix - a digital compass with NMEA2000](https://open-boat-projects.org/en/magnetix-ein-digitaler-kompass-mit-nmea2000/).

ESP32 Webserver [Beginner's Guide](https://randomnerdtutorials.com/esp32-web-server-beginners-guide/).

No paid partnerships.

Developed by Matti Vesala in collaboration with ChatGPT and Claude. ChatGPT was used as sparring partner for ideas, for generating source code skeletons and as my personal trainer in C++ until it started wild hallusinations at model 5.1. Claude (code) was used for code review (less hallusination than ChatGPT).

The only "full AI" pieces of code are `computeHarmonicCoeffs(..)` and `computeDeviation(..)` functions while `WebUIManager::handleRoot()`, `WebUIManager::handleRestart()` and `WebUIManager::handleDeviationTable()` are heavily "AI assisted".

I have no clue whatsover how these LLMs generate source code. Any similarities to any other source code out there, done by other people or organizations, is purely coincidental and unintentional from my side.

I would highly appreciate improvement suggestions as well as any Arduino-style ESP32/C++ coding advice before entering into SensESP/PlatformIO universe in my next project. 😃 

## Gallery

<img src="project1.jpeg" width="120"> <img src="project2.jpeg" width="120"> <img src="project3.jpeg" width="120"> <img src="project4.jpeg" width="120">







