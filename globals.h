#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_system.h>
#include "CalMode.h"
#include "harmonic.h"
#include "secrets.h"

using namespace websockets;

extern const uint8_t CMPS14_ADDR;                     // I2C address of CMPS14

// SH-ESP32 default pins for I2C
extern const uint8_t I2C_SDA;
extern const uint8_t I2C_SCL;

// Webserver
// extern WebServer server;

// Return float validity
inline bool validf(float x) { return !isnan(x) && isfinite(x); }