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
#include <esp_mac.h>
#include "CalMode.h"
#include "WifiState.h"
#include "harmonic.h"
#include "secrets.h"

inline constexpr const char* FW_VERSION = "N/A";
using namespace websockets;