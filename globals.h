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