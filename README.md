# CMPS14-ESP32-SignalK Gateway

ESP32-based reader for CMPS14 compass & attitude sensor of Robot Electronics.
Sends heading, pitch and roll (including maximum +/- values) to SignalK server via websocket/json.
Applies installation offset, deviation and magnetic variation to raw compass heading to determine magnetic and optionally true heading.
Subscribes magnetic variation from SignalK server which will be prioritized over manually entered variation to determine true heading.
Uses harmonic model to calculate deviation at any given compass heading, based on user-measured deviations at 8 cardinal and intercardinal directions.
Uses LCD 16x2 to show status messages and heading. If no wifi around, runs on LCD only mode.
Runs a webserver to provide configuration web UI.
Configurable parameters: calibration mode (full auto, auto, manual), installation offset, measured deviations, manual variation and heading mode (true, magnetic).
OTA enabled.
Preferences enabled.
Led indicators for calibration mode and connection status (two leds).

## Features

### Compass and attitude

1. Reads angle, pitch and roll from CMPS14 at ~20 Hz cycles
2. Applies installation offset resulting in compass heading
3. Applies smoothing to compass heading
4. Computes magnetic heading using harmonic deviation model on the compass heading
5. Computes true heading (optional), using either
   - Live navigation.magneticVariation received from SignalK (prioritized over user input)
   - Manual variation from user input on web UI (used if navigation.magneticVariation is not available)

### Deviation curve

1. Uses 8 user-measured deviations (N, NE, E, SE, S, SW, W, NW) from web UI
2. Fits 5-parameter sinusoidal model: A + Bsin(hdg) + Ccos(hdg) + Dsin(2hdg) + Ecos(2hdg)
3. Stores 5 coeffs persistently in ESP32 Preferences
4. Full deviation curve and deviation table available via web UI
   - Deviation curve as SVG graph
   - Deviation table in 10° steps

### Ddlkjfasjf



