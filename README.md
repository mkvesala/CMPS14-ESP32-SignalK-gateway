# CMPS14-ESP32-SignalK Gateway

ESP32-based reader for Robot Electronics [CMPS14](https://www.robot-electronics.co.uk/files/cmps14.pdf) compass & attitude sensor. Sends heading, pitch and roll (including min and max) to [SignalK](https://signalk.org) server via websocket/json.

Applies installation offset, deviation and magnetic variation to raw angle to determine compass heading, magnetic heading and optionally true heading. Subscribes magnetic variation from SignalK server. This is prioritized over manually entered variation to determine true heading. Uses harmonic model to compute deviation at any given compass heading, based on user-measured deviations at 8 cardinal and intercardinal directions.

Uses LCD 16x2 to show status messages and heading. If no wifi around, runs on LCD only mode.

Runs a webserver to provide configuration web UI. Configurable parameters: calibration mode (full auto, auto, manual), installation offset, measured deviations, manual variation and heading mode (true, magnetic).

OTA and Preferences enabled.

Led indicators for calibration mode and connection status (two leds).

## Features

### Compass and attitude

1. Reads angle, pitch and roll from CMPS14 at ~20 Hz cycle
2. Applies installation offset (user input) to raw angle for compass heading
3. Applies smoothing to compass heading
4. Computes magnetic heading using harmonic deviation model on the compass heading
5. Computes true heading (optional), using either
   - Live *navigation.magneticVariation* received from SignalK (prioritized over user input)
   - Manual variation from user input on web UI (used automatically whenever *navigation.magneticVariation* is not available)

### Deviation

1. Takes 8 user-measured deviations (N, NE, E, SE, S, SW, W, NW) as input from web UI
2. Fits 5-parameter sinusoidal model: A + Bsin(hdg) + Ccos(hdg) + Dsin(2hdg) + Ecos(2hdg)
3. Stores user-measured deviations and computed 5 coeffs persistently in ESP32 Preferences
4. Full deviation curve and deviation table available via web UI
   - Deviation curve as SVG graph
   - Deviation table with 10° steps

**Note that deviation can be applied only to a permanently mounted stable compass. While CMPS14 can be securely mounted to the vessel, it's behavior may be altered by calibration (automatic or manual). It is recommended to keep deviation at 0° until there are undeniable evidence that the compass is stable and operates without any needs for regular calibration. It's obvious that the deviations should always be re-measured and computed after each calibration.**

### Magnetic variation

1. Subscribes *navigation.magneticVariation* path from SignalK server. This is treated as the primary and most trusted source of magnetic variation.
2. User may enter magnetic variation manually on the web UI. This is a backup and will be used automatically if variation is not available at SignalK path.
3. Magnetic variation is used for computing true heading on magnetic heading.

**Note that when the SignalK connection is open, the magnetic heading will always be sent to SignalK *navigation.headingMagnetic* path regardless of the active heading mode (true/magnetic). It is a standard practise to compute true heading on server side using SignalK [Derived Data](https://github.com/SignalK/signalk-derived-data) plugin or similar, or on other clients such as [OpenCPN](https://opencpn.org) using [WMM](https://www.ncei.noaa.gov/products/world-magnetic-model).**

### SignalK output

Sends on maximum ~10 Hz frequency, in radians, with a deadband of 0.25°:

1. *navigation.headingMagnetic*
2. *navigation.attitude.pitch*
3. *navigation.attitude.roll*
4. (optionally) *navigation.headingTrue*

Sends on maximum ~1 Hz frequency, in radians, only if changed:

1. *navigation.attitude.pitch.max*
2. *navigation.attitude.pitch.min*
3. *navigation.attitude.roll.max*
4. *navigation.attitude.roll.min*

### Calibration modes

Three supported calibration modes:

1. *FULL AUTO* - CMPS14's built-in autocalibration & autosave + optional user configurable stop timer / manual stop
2. *AUTO* - auto-detect good enough calibration to save automatically
3. *MANUAL* - user-triggered calibration with manual save/replace

In *USE* mode the CMPS14 operates normally based on saved calibration profile (no calibration is running).

When calibration is running, *SYS*, *ACC* and *MAG* indicators are monitored at ~2 Hz cycles. In *AUTO* mode the calibration profile will be saved when all three indicators equal 3 (best) over three consecutive cycles. In *MANUAL* mode user may decide when to save by monitoring the values changing on web UI status block.

**Note that the *GYR* indicator for gyro is not monitored. There is a reported firmware bug in CMPS14 that makes *GYR* indicator unreliable.**

### Web UI (ESP32 Webserver)








