# CMPS14-ESP32-SignalK Gateway

ESP32-based reader for Robot Electronics [CMPS14](https://www.robot-electronics.co.uk/files/cmps14.pdf) compass & attitude sensor. Sends heading, pitch and roll (including min and max) to SignalK server via websocket/json.

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

**Note that deviation should be applied only to a permanently mounted stable compass. While CMPS14 can be securely mounted to the vessel, it's behavior may be altered by calibration (automatic or manual). It is recommended to keep deviation at 0° until there are undeniable evidence that the compass is stable and operates without any needs for regular calibration. It's obvious that the deviations should always be re-measured and computed after each calibration.**

### Magnetic variation





