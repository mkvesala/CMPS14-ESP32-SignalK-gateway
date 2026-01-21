#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include "harmonic.h"
#include "CalMode.h"
#include "CMPS14Processor.h"
#include "CMPS14Preferences.h"
#include "SignalKBroker.h"
#include "DisplayManager.h"
#include "version.h"

// === W E B U I M A N A G E R  C L A S S ===
//
// - Class WebUIManager - "the webui" responsible for providing
//   a web based UI for the user to configure the compass
// - Session based authentication with SHA256 password and
//   random 128-bit session token
// - Init (start the WebServer): webui.begin()
// - Handle client request: webui.handleRequest() - this actually
//   wraps WebServer.handleClient() to be called in loop()
// - The web UI: http://<yourESP32ipaddress>
// - Descriptions of the endpoints and UI in README file
// - Uses:
//   - CMPS14Processor
//   - CMPS14Preferences
//   - SignalKBroker
//   - DisplayManager
//   - CalMode
// - Owns: WebServer
 
class WebUIManager {

public:

  explicit WebUIManager(CMPS14Processor &compassref, CMPS14Preferences &compass_prefsref, SignalKBroker &signalkref, DisplayManager &displayref);

  void begin();
  void handleRequest();

  void setLoopRuntimeInfo(float avg_us); // Debug

private:
  
  WebServer server;
  CMPS14Processor &compass;
  CMPS14Preferences &compass_prefs;
  SignalKBroker &signalk;
  DisplayManager &display;

  // Reusable JSON document
  StaticJsonDocument<1024> status_doc;

  // Debug app.loop() runtime
  float runtime_avg_us = 0.0f;

  // Webserver endpoint handlers
  void setupRoutes();
  void handleStatus();
  void handleSetOffset();
  void handleSetDeviations();
  void handleSetCalmode();
  void handleSetMagvar();
  void handleSetHeadingMode();
  void handleRoot();
  void handleDeviationTable();
  void handleRestart();
  void handleStartCalibration();
  void handleStopCalibration();
  void handleSaveCalibration();
  void handleReset();
  void handleLevel();
  void handleLogin();
  void handleLoginPage();
  void handleLogout();
  void handleChangePassword();
  void handleChangePasswordPage();


  // 128-bit random token (32 hex chars + null)
  struct Session {
    char token[33];
    unsigned long created_ms; 
    unsigned long last_seen_ms;
  };
  
  static constexpr uint8_t MAX_SESSIONS = 3;
  static constexpr unsigned long SESSION_TIMEOUT_MS = 21600000; // 6 hours
  static const char* HEADER_KEYS[1];
  
  Session sessions[MAX_SESSIONS];
  
  // Authentication
  bool requireAuth();
  bool isAuthenticated();
  bool validateSession(const char* token);
  char* createSession();
  void cleanExpiredSessions();

  // Cookie parser helper
  bool parseSessionToken(const char* cookies, char* token_out_33bytes);
  
  // SHA256 hash helper
  void sha256Hash(const char* input, char* output_hex_64bytes);

  // Milliseconds to hh:mm:ss
  const char* ms_to_hms_str(unsigned long ms) {
    static char buf[12];
    unsigned long total_secs = ms / 1000;
    unsigned int h = total_secs / 3600;
    unsigned int m = (total_secs % 3600) / 60;
    unsigned int s = (total_secs % 60);
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
    return buf;
  }

};