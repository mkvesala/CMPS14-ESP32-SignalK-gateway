#include "WebUIManager.h"
#include "secrets.h"

// Value for static const char* array
const char* WebUIManager::HEADER_KEYS[1] = {"Cookie"};

// === P U B L I C ===

// Constructor
WebUIManager::WebUIManager(
    CMPS14Processor &compassref,
    CMPS14Preferences &compass_prefsref,
    SignalKBroker &signalkref,
    DisplayManager &displayref
    ) : server(80),
        compass(compassref),
        compass_prefs(compass_prefsref), 
        signalk(signalkref),
        display(displayref) {
          for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
            sessions[i].token[0] = '\0';
            sessions[i].created_ms = 0;
            sessions[i].last_seen_ms = 0;
          }
          for (uint8_t i = 0; i < MAX_IP_FOLLOWUP; i++) {
            login_attempts[i].ip_address = 0;
            login_attempts[i].timestamp_ms = 0;
            login_attempts[i].count = 0;
          }
        }

// Init the webserver
void WebUIManager::begin() {
  char stored_hash[65];
  char default_hash[65];
  this->sha256Hash(DEFAULT_WEB_PASSWORD, default_hash);
  if (!compass_prefs.loadWebPasswordHash(stored_hash)) {
    // If no password in NVS, save default
    compass_prefs.saveWebPassword(default_hash);
    display.showInfoMessage("DEFAULT PASSWORD!", "CHANGE NOW!");
  } else if (strcmp(stored_hash, default_hash) == 0) {
    // If NVS password equals the default, show warning
    display.showInfoMessage("DEFAULT PASSWORD!", "CHANGE NOW!");
  }
  server.collectHeaders(HEADER_KEYS, 1);
  this->setupRoutes();
  server.begin();
}

// Handle client request
void WebUIManager::handleRequest() {
  server.handleClient();
}

// Debug: follow up app.loop() runtime stats
void WebUIManager::setLoopRuntimeInfo(float avg_us) {
  runtime_avg_us = avg_us;
}

// === P R I V A T E ===

// Set the handlers for webserver endpoints
void WebUIManager::setupRoutes() {
  
  // No authentication
  server.on("/", HTTP_GET, [this]() {
    if (this->isAuthenticated()) {
      server.sendHeader("Location", "/config");
      server.send(302, "text/plain", "");
    } else this->handleLoginPage();
  });
  server.on("/login", HTTP_POST, [this]() {
    this->handleLogin();
  });
  server.on("/logout", HTTP_GET, [this]() {
    this->handleLogout();
  });
  server.on("/changepassword", HTTP_POST, [this]() {
    this->handleChangePassword();
  });

  // Requires authentication
  server.on("/config", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleRoot();
  });
  server.on("/status", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleStatus();
  });
  server.on("/cal/on", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleStartCalibration();
  });
  server.on("/cal/off", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleStopCalibration();
  });
  server.on("/store/on", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleSaveCalibration();
  });
  server.on("/reset/on", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleReset();
  });
  server.on("/offset/set", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleSetOffset();
  });
  server.on("/dev8/set", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleSetDeviations();
  });
  server.on("/calmode/set", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleSetCalmode();
  });
  server.on("/magvar/set", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleSetMagvar();
  });
  server.on("/heading/mode", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleSetHeadingMode();
  });
  server.on("/restart", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleRestart();
  });
  server.on("/deviationdetails", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleDeviationTable();
  });
  server.on("/level", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleLevel();
  });
  server.on("/changepassword", HTTP_GET, [this]() {
    if (!this->requireAuth()) return;
    this->handleChangePasswordPage();
  });

}

// Web UI handler for CALIBRATE button
void WebUIManager::handleStartCalibration(){
  if (compass.startCalibration(CalMode::MANUAL)) {
    // ok
  }
  this->handleRoot();
}

// Web UI handler for STOP button
void WebUIManager::handleStopCalibration(){
  if (compass.stopCalibration()) {
    // ok
  }
  this->handleRoot();
}

// Web UI handler for SAVE button
void WebUIManager::handleSaveCalibration(){
  if (compass.saveCalibrationProfile()) {
    // ok
  }
  this->handleRoot();
}

// Web UI handler for RESET button
void WebUIManager::handleReset(){
  if (compass.reset()) {
    // ok
  }
  this->handleRoot(); 
}

// Web UI handler for LEVEL CMPS14 button
void WebUIManager::handleLevel(){
  compass.level();
  char line2[17];
  snprintf(line2, sizeof(line2), "P:%5.1f R:%5.1f", compass.getPitchLevel(), compass.getRollLevel());
  display.showInfoMessage("LEVEL CMPS14", line2);
  this->handleRoot(); 
}

// Web UI handler for status block, build json with appropriate data
void WebUIManager::handleStatus() {
  
  uint8_t mag = 255, acc = 255, gyr = 255, sys = 255;
  uint8_t statuses[4];
  compass.requestCalStatus(statuses);
  mag = statuses[0];
  acc = statuses[1];
  gyr = statuses[2];
  sys = statuses[3]; 

  // Debug memory usage
  uint16_t heap_free = ESP.getFreeHeap() / 1024;    // kbytes
  uint16_t heap_total = ESP.getHeapSize() / 1024;   // kbytes
  uint8_t heap_percent = (heap_free * 100) / heap_total;
  UBaseType_t stack_free = uxTaskGetStackHighWaterMark(NULL); // bytes in ESP-IDF, unlike vanilla FreeRTOS

  HarmonicCoeffs hc = compass.getHarmonicCoeffs();
  status_doc.clear();

  status_doc["cal_mode"]             = calModeToString(compass.getCalibrationModeRuntime());
  status_doc["cal_mode_boot"]        = calModeToString(compass.getCalibrationModeBoot());
  status_doc["fa_left"]              = this->ms_to_hms_str(compass.getFullAutoLeft());
  status_doc["wifi"]                 = display.getWifiIPAddress();
  status_doc["rssi"]                 = display.getWifiQuality();
  status_doc["hdg_deg"]              = compass.getHeadingDeg();
  status_doc["compass_deg"]          = compass.getCompassDeg();
  status_doc["pitch_deg"]            = compass.getPitchDeg();
  status_doc["roll_deg"]             = compass.getRollDeg();
  status_doc["pitch_level"]          = compass.getPitchLevel();
  status_doc["roll_level"]           = compass.getRollLevel();
  status_doc["offset"]               = compass.getInstallationOffset();
  status_doc["dev"]                  = compass.getDeviation();
  status_doc["variation"]            = compass.getVariation();
  status_doc["heading_true_deg"]     = compass.getHeadingTrueDeg();
  status_doc["acc"]                  = acc;
  status_doc["mag"]                  = mag;
  status_doc["sys"]                  = sys;
  status_doc["hca"]                  = hc.A;
  status_doc["hcb"]                  = hc.B;
  status_doc["hcc"]                  = hc.C;
  status_doc["hcd"]                  = hc.D;
  status_doc["hce"]                  = hc.E;
  status_doc["use_manual_magvar"]    = compass.isUsingManualVariation();   
  status_doc["send_hdg_true"]        = compass.isSendingHeadingTrue();         
  status_doc["stored"]               = compass.isCalProfileStored();
  status_doc["version"]              = SW_VERSION;
  status_doc["firmware"]             = compass.getFwVersion();
  // Debug
  status_doc["heap_free"]            = heap_free;
  status_doc["heap_total"]           = heap_total; 
  status_doc["heap_percent"]         = heap_percent;
  status_doc["stack_free"]           = stack_free;
  status_doc["runtime_avg"]          = runtime_avg_us;
  status_doc["uptime"]               = this->ms_to_hms_str(millis());

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  char out[1048];
  size_t n = serializeJson(status_doc, out, sizeof(out));
  server.send(200, "application/json; charset=utf-8", out);
}

// Web UI handler for installation offset, to correct raw compass heading
void WebUIManager::handleSetOffset() {
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (!validf(v)) v = 0.0f;
    if (v < -180.0f) v = -180.0f;
    if (v >  180.0f) v =  180.0f;

    compass.setInstallationOffset(v);

    // Save prefrences permanently
    compass_prefs.saveInstallationOffset(v);

    char line2[17];
    snprintf(line2, sizeof(line2), "SAVED %5.0f%c", v, 223);
    display.showInfoMessage("INSTALL OFFSET", line2);
  }
  this->handleRoot();
}

// Web UI handler for 8 measured deviation values, to correct raw compass heading --> navigation.headingMagnetic
void WebUIManager::handleSetDeviations() {
  auto getf = [&](const char* k) -> float {
    if (!server.hasArg(k)) return 0.0f;
    float v = server.arg(k).toFloat();
    if (!validf(v)) v = 0.0f;
    if (v < -90.0f) v = -90.0f;
    if (v >  90.0f) v =  90.0f;
    return v;
  };
  float measured_deviations[8];
  measured_deviations[0] = getf("N");
  measured_deviations[1] = getf("NE");
  measured_deviations[2] = getf("E");
  measured_deviations[3] = getf("SE");
  measured_deviations[4] = getf("S");
  measured_deviations[5] = getf("SW");
  measured_deviations[6] = getf("W");
  measured_deviations[7] = getf("NW");

  // Calculate 5 coeffs
  compass.setMeasuredDeviations(measured_deviations);
  HarmonicCoeffs hc = computeHarmonicCoeffs(measured_deviations); 
  compass.setHarmonicCoeffs(hc);

  compass_prefs.saveDeviationSettings(measured_deviations, hc);

  display.showSuccessMessage("SAVE DEVIATIONS", true);

  this->handleRoot();
}

// Web UI handler to choose calibration mode on boot
void WebUIManager::handleSetCalmode() {
  if (server.hasArg("c") && server.hasArg("t")) { 
    
    char c = server.arg("c").charAt(0);
    CalMode v = CalMode::USE;
    if (c == '0') v = CalMode::FULL_AUTO;
    else if (c == '1') v = CalMode::AUTO;
    else if (c == '2') v = CalMode::MANUAL;
    else v = CalMode::USE;
    compass.setCalibrationModeBoot(v);
   
    long t = server.arg("t").toInt();
    if (t <= 0) t = 0;
    if (t > 60) t = 60;
    unsigned long full_auto_stop_ms = 60 * 1000 * t;
    compass.setFullAutoTimeout(full_auto_stop_ms);

    compass_prefs.saveCalibrationSettings(v, full_auto_stop_ms);
    display.showInfoMessage("BOOT MODE SAVED", calModeToString(v));
  }
  this->handleRoot();
}

// Web UI handler to set magnetic variation manually.
void WebUIManager::handleSetMagvar() {
  
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (!validf(v)) v = 0.0f;
    if (v < -90.0f) v = -90.0f;
    if (v >  90.0f) v =  90.0f;
    compass.setManualVariation(v);

    compass_prefs.saveManualVariation(v);

    char line2[17];
    snprintf(line2, sizeof(line2), "SAVED %5.0f%c %c", fabs(v), 223, (v >= 0 ? 'E':'W'));
    display.showInfoMessage("MAG VARIATION", line2);
  }
  this->handleRoot();
}

// Web UI handler to set heading mode TRUE or MAGNETIC
void WebUIManager::handleSetHeadingMode() {
  bool send_hdg_true = false;
  if (server.hasArg("m")) {
    char m = server.arg("m").charAt(0);
    send_hdg_true = (m == '1');
  }
  compass.setSendHeadingTrue(send_hdg_true);

  compass_prefs.saveSendHeadingTrue(send_hdg_true);

  display.showInfoMessage("HDG MODE SAVED", send_hdg_true ? "TRUE" : "MAGNETIC");
  this->handleRoot();
}

// Web UI handler for the configuration HTML page 
void WebUIManager::handleRoot() {

  // Re-usable buffer for all content creation
  char buf[256];

  CalMode mode_runtime = compass.getCalibrationModeRuntime();
  CalMode mode_boot = compass.getCalibrationModeBoot();
  float measured_deviations[8];
  compass.getMeasuredDeviations(measured_deviations);
  bool send_hdg_true = compass.isSendingHeadingTrue();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html; charset=utf-8", "");

  // Head and CSS
  server.sendContent_P(R"(
    <!DOCTYPE html><html><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=5, user-scalable=yes"><link rel="icon" href="data:,">
    <style>
    * { box-sizing: border-box } 
    html { font-family: Helvetica; margin: 0; padding: 0; text-align: center; }
    body { background:#000; color:#fff; max-width: 768px; margin: 0 auto; padding: 0; font-size: clamp(8px, 3vmin, 14px); }
    .button { background-color: #00A300; border: none; color: white; padding: 6px 10px; text-decoration: none; font-size: clamp(8px, 3vmin, 14px); margin: 2px; cursor: pointer; border-radius:6px; text-align:center}
    .button2 { background-color: #D10000; }
    .button:disabled, .button2:disabled { opacity:0.5; cursor:not-allowed; }
    .card { width:92%; margin:2px auto; padding:2px; background:#0b0b0b; border-radius:6px; box-shadow:0 0 0 1px #222 inset; }
    h1 { margin:12px 0 8px 0; } h2 { margin:8px 0; font-size: clamp(10px, 4vmin, 16px); } h3 { margin:6px 0; font-size: clamp(8px, 3vmin, 14px); }
    label { display:inline-block; min-width:40px; text-align:right; margin-right:6px; }
    input[type=number]{ font-size: clamp(8px, 3vmin, 14px); width:60px; padding:4px 6px; margin:4px; border-radius:6px; border:1px solid #333; background:#111; color:#fff; }
    #st { font-size: clamp(6px, 2vmin, 10px); line-height: 1.2; color: #DBDBDB; background-color: #000; padding: 8px; border-radius: 6px; width: 90%; margin: auto; text-align: center; white-space: pre-line; font-family: monospace;}
    </style></head><body>
    <h2><a href="/" style="color:white; text-decoration:none;">CMPS14 CONFIG</a></h2>
    )");

  // DIV Calibrate, Stop, Reset
  server.sendContent_P(R"(
    <div class='card' id='controls'>)");
  if (mode_runtime == CalMode::FULL_AUTO) {
    snprintf(buf, sizeof(buf), "Current mode: %s (%s)<br>", calModeToString(mode_runtime), this->ms_to_hms_str(compass.getFullAutoLeft()));
    server.sendContent(buf);
  } else {
    snprintf(buf, sizeof(buf), "Current mode: %s<br>", calModeToString(mode_runtime));
    server.sendContent(buf);
  }

  if (mode_runtime == CalMode::AUTO || mode_runtime == CalMode::MANUAL) {
    server.sendContent_P(R"(<a href="/cal/off"><button class="button button2">STOP</button></a>)");
    if (!compass.isCalProfileStored()) {
      server.sendContent_P(R"(<a href="/store/on"><button class="button">SAVE</button></a>)");
    } else {
      server.sendContent_P(R"(<a href="/store/on"><button class="button button2">REPLACE</button></a>)");
    }
  } else if (mode_runtime == CalMode::USE) {
    server.sendContent_P(R"(<a href="/cal/on"><button class="button">CALIBRATE</button></a>)");
  } else if (mode_runtime == CalMode::FULL_AUTO) {
    server.sendContent_P(R"(<a href="/cal/off"><button class="button button2">STOP</button></a>)");
  }
  server.sendContent_P(R"(<a href="/reset/on"><button class="button button2">RESET</button></a></div>)");

  // DIV Calibration mode on boot
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/calmode/set" method="get">
    <label>Boot mode </label><label><input type="radio" name="c" value="0")");
  if (mode_boot == CalMode::FULL_AUTO) server.sendContent_P(R"( checked)");
  server.sendContent_P(R"(>Full auto </label><label>
    <input type="radio" name="c" value="1")");
  if (mode_boot == CalMode::AUTO) server.sendContent_P(R"( checked)");
  server.sendContent_P(R"(>Auto </label><label>
    <input type="radio" name="c" value="3")");
  if (mode_boot == CalMode::USE) server.sendContent_P(R"( checked)");
  server.sendContent_P(R"(>Use/Manual</label><br>
    <label>Full auto stops in </label>
    <input type="number" name="t" step="1" min="0" max="60" value=")");
  float to = (float)(compass.getFullAutoTimeout()/1000/60);
  snprintf(buf, sizeof(buf), "%.0f", to);
  server.sendContent(buf);
  server.sendContent_P(R"("> mins (0 never)<br><input type="submit" id="calmodebtn" class="button" value="SAVE"></form></div>)");

  // DIV Set installation offset
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/offset/set" method="get">
    <label>Installation offset</label>
    <input type="number" name="v" step="1" min="-180" max="180" value=")");
  snprintf(buf, sizeof(buf), "%.0f", compass.getInstallationOffset());
  server.sendContent(buf);
  server.sendContent_P(R"(">&deg; <input type="submit" value="SAVE" class="button"></form></div>)");

  // DIV Set deviation 
  server.sendContent_P(R"(
    <div class='card'>Measured deviations<form action="/dev8/set" method="get"><div>)");

  // Row 1: N NE
  snprintf(buf, sizeof(buf),
    "<label>N</label><input name=\"N\"  type=\"number\" step=\"1\" value=\"%.0f\">&deg; "
    "<label>NE</label><input name=\"NE\" type=\"number\" step=\"1\" value=\"%.0f\">&deg; ",
    measured_deviations[0], measured_deviations[1]);
  server.sendContent(buf);
  server.sendContent_P(R"(</div><div>)");

  // Row 2:  E SE
  snprintf(buf, sizeof(buf),
    "<label>E</label><input name=\"E\"  type=\"number\" step=\"1\" value=\"%.0f\">&deg; "
    "<label>SE</label><input name=\"SE\" type=\"number\" step=\"1\" value=\"%.0f\">&deg; ",
    measured_deviations[2], measured_deviations[3]);
  server.sendContent(buf);
  server.sendContent_P(R"(</div><div>)");

  // Row 3: S SW
  snprintf(buf, sizeof(buf),
    "<label>S</label><input name=\"S\"  type=\"number\" step=\"1\" value=\"%.0f\">&deg; "
    "<label>SW</label><input name=\"SW\" type=\"number\" step=\"1\" value=\"%.0f\">&deg; ",
    measured_deviations[4], measured_deviations[5]);
  server.sendContent(buf);
  server.sendContent_P(R"(</div><div>)");

  // Row 4: W NW
  snprintf(buf, sizeof(buf),
    "<label>W</label><input name=\"W\"  type=\"number\" step=\"1\" value=\"%.0f\">&deg; "
    "<label>NW</label><input name=\"NW\" type=\"number\" step=\"1\" value=\"%.0f\">&deg; ",
    measured_deviations[6], measured_deviations[7]);
  server.sendContent(buf);

  server.sendContent_P(R"(
    </div>
    <input type="submit" class="button" value="SAVE"></form></div>)");

  // DIV Deviation curve
  server.sendContent_P(R"(
    <div class='card'>
    <a href="/deviationdetails"><button class="button">SHOW DEVIATION CURVE</button></a></div>)");

  // DIV Set variation 
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/magvar/set" method="get">
    <label>Manual variation </label>
    <input type="number" name="v" step="1" min="-180" max="180" value=")");
  snprintf(buf, sizeof(buf), "%.0f", compass.getManualVariation());
  server.sendContent(buf);
  server.sendContent_P(R"(">&deg; <input type="submit" value="SAVE" class="button"></form></div>)");

  // DIV Set heading mode TRUE or MAGNETIC
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/heading/mode" method="get">
    <label>Heading </label><label><input type="radio" name="m" value="1")");
    if (send_hdg_true) server.sendContent_P(R"( checked)");
    server.sendContent_P(R"(>True</label><label>
    <input type="radio" name="m" value="0")");
    if (!send_hdg_true) server.sendContent_P(R"( checked)");
    server.sendContent_P(R"(>Magnetic</label>
    <input type="submit" class="button" value="SAVE"></form></div>)");

  // DIV Level attitude
  server.sendContent_P(R"(<div class='card'>
    <a href="/level"><button class="button">LEVEL ATTITUDE</button></a></div>)");

  // DIV Status
  server.sendContent_P(R"(<div class='card'><div id="st">Loading...</div></div>)");

  // Live JS updater script
  server.sendContent_P(R"(
    <script>
      function fmt0(x) {
        return (x === null || x === undefined || Number.isNaN(x)) ? 'NA' : x.toFixed(0);
      }
      function fmt1(x) {
        return (x === null || x === undefined || Number.isNaN(x)) ? 'NA' : x.toFixed(1);
      }
      function renderControls(j) {
        const el = document.getElementById('controls');
        if (!el || !j) return;
        let html = '';
        if (j.cal_mode === 'FULL AUTO') {
          html += `Current mode: ${j.cal_mode} (${j.fa_left})<br>`;
        } else {
          html += `Current mode: ${j.cal_mode}<br>`;
        }
        if (j.cal_mode === 'AUTO' || j.cal_mode === 'MANUAL') {
          html += `<a href="/cal/off"><button class="button button2">STOP</button></a>`;
          if (j.stored) {
            html += `<a href="/store/on"><button class="button button2">REPLACE</button></a>`;
          } else {
            html += `<a href="/store/on"><button class="button">SAVE</button></a>`;
          }
        } else if (j.cal_mode === 'USE') {
          html += `<a href="/cal/on"><button class="button">CALIBRATE</button></a>`;
        } else if (j.cal_mode === 'FULL AUTO') {
          html += `<a href="/cal/off"><button class="button button2">STOP</button></a>`;
        }
        html += `<a href="/reset/on"><button class="button button2">RESET</button></a>`;
        el.innerHTML = html;
      }
      function upd(){
        fetch('/status').then(r=>{
          if(r.status === 401){
            location.replace('/');
            return;
          }
          return r.json();
        }).then(j=>{
          const d=[
            'Installation offset: '+fmt0(j.offset)+'\u00B0',
            'Heading (C): '+fmt0(j.compass_deg)+'\u00B0',
            'Deviation: '+fmt0(j.dev)+'\u00B0',
            'Heading (M): '+fmt0(j.hdg_deg)+'\u00B0',
            'Variation: '+fmt0(j.variation)+'\u00B0',
            'Heading (T): '+fmt0(j.heading_true_deg)+'\u00B0',
            'Pitch: '+fmt1(j.pitch_deg)+'\u00B0 ('+fmt1(j.pitch_level)+'\u00B0) Roll: '+fmt1(j.roll_deg)+'\u00B0 ('+fmt1(j.roll_level)+'\u00B0)',
            'Acc: '+j.acc+', Mag: '+j.mag+', Sys: '+j.sys,
            'HcA: '+fmt1(j.hca)+', HcB: '+fmt1(j.hcb)+', HcC: '+fmt1(j.hcc)+', HcD: '+fmt1(j.hcd)+', HcE: '+fmt1(j.hce),
            'Heap: '+j.heap_free+' kB ('+j.heap_percent+' \u0025) free, total '+j.heap_total+' kB',
            'Loop runtime avg: '+fmt1(j.runtime_avg)+' \u00B5s, loop task free stack: '+j.stack_free+' B',
            'WiFi: '+j.wifi+' ('+j.rssi+')',
            'SW release: '+j.version+', FW version: '+j.firmware,
            'System uptime: '+j.uptime
          ];
          document.getElementById('st').textContent=d.join('\n');
          renderControls(j);
          const btn = document.getElementById('calmodebtn');
          btn.disabled = (j.cal_mode === 'FULL AUTO');
        }).catch(_=>{
          document.getElementById('st').textContent='Status fetch failed';
        });
      }
      setInterval(upd,1013);upd();
    </script>)");
  
  // DIV System buttons
  server.sendContent_P(R"(
    <div class='card'>
    <a href="/changepassword"><button class="button">CHANGE PASSWORD</button></a>
    <a href="/logout"><button class="button button2">LOGOUT</button></a>
    <a href="/restart?ms=5003"><button class="button button2">RESTART</button></a></div>
    </body>
    </html>)");
  server.sendContent("");
}

// WebUI handler to draw deviation table and deviation curve
void WebUIManager::handleDeviationTable(){
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent_P(R"(
    <!DOCTYPE html><html><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=5, user-scalable=yes">
    <link rel="icon" href="data:,">
    <title>Deviation details</title>
    <style>
      * { box-sizing: border-box } 
      html { font-family: Helvetica; margin: 0; padding: 0; text-align: center; }
      body{background:#000; color:#fff; max-width: 768px; margin: 0 auto; padding: 0;font-size: clamp(8px, 3vmin, 14px); background:#000; color:#fff; font-family:Helvetica;margin:0 auto; text-align:center}
      .card{font-size: clamp(8px, 3vmin, 14px); width:92%; margin:8px auto; padding:8px;background:#0b0b0b;border-radius:6px;box-shadow:0 0 0 1px #222 inset}
      table{font-size:clamp(8px, 3vmin, 14px); margin:8px auto; border-collapse:collapse;color:#ddd}
      td,th{font-size:clamp(8px, 3vmin, 14px); border:1px solid #333; padding:4px 8px}
      h2{margin:8px 0; font-size: clamp(10px, 4vmin, 16px);}
      a{color:#fff; text-decoration:none;}
    </style>
    </head><body>
    <div class="card">
  )");

  // SVG settings
  const int W=800, H=400;
  const float xpad=40, ypad=20;
  const float xmin=0, xmax=360;
  const int STEP = 10; // Sample every 10°
  
  const DeviationLookup &dev_lut = compass.getDeviationLookup();
  
  // Use pre-calclulated deviations
  float ymax = 0.0f;
  for (int i=0; i < 360; i += STEP){
    float dev = dev_lut.lookup((float)i);
    if (fabs(dev) > ymax) ymax = fabs(dev);
  }
  ymax = max(ymax + 1.0f, 5.0f); 

  auto xmap = [&](float x){ return xpad + (x - xmin) * ( (W-2*xpad) / (xmax-xmin) ); };
  auto ymap = [&](float y){ return H-ypad - (y + ymax) * ( (H-2*ypad) / (2*ymax) ); };

  // Grid
  char buf[256];
  server.sendContent_P(R"(<svg width="100%" viewBox="0 0 )");
  snprintf(buf, sizeof(buf), "%d %d", W, H);
  server.sendContent(buf);
  server.sendContent_P(R"(" preserveAspectRatio="xMidYMid meet" style="background:#000">
    <rect x="0" y="0" width="100%" height="100%" fill="#000"/>
  )");

  // X-axis
  snprintf(buf, sizeof(buf),
    "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#444\"/>",
    xmap(xmin), ymap(0), xmap(xmax), ymap(0));
  server.sendContent(buf);

  // Y-axis
  snprintf(buf, sizeof(buf),
    "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#444\"/>",
    xmap(0), ymap(-ymax), xmap(0), ymap(ymax));
  server.sendContent(buf);

  // Grid
  for (int k=0;k<=360;k+=45){
    float X = xmap(k);
    snprintf(buf,sizeof(buf),
      "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#222\"/>",
      X, ymap(-ymax), X, ymap(ymax));
    server.sendContent(buf);
    snprintf(buf,sizeof(buf),
      "<text x=\"%.1f\" y=\"%.1f\" fill=\"#aaa\" font-size=\"10\" text-anchor=\"middle\">%03d</text>",
      X, ymap(-ymax)-4, k);
    server.sendContent(buf);
  }

  // Y-axis values
  for (int j=(int)ceil(-ymax); j<= (int)floor(ymax); j++){
    float Y = ymap(j);
    snprintf(buf,sizeof(buf),
      "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#222\"/>",
      xmap(xmin), Y, xmap(xmax), Y);
    server.sendContent(buf);
    snprintf(buf,sizeof(buf),
      "<text x=\"%.1f\" y=\"%.1f\" fill=\"#aaa\" font-size=\"10\" text-anchor=\"end\">%+d°</text>",
      xmap(xmin)-6, Y+4, j);
    server.sendContent(buf);
  }

  // Polyline (every 10 degrees for performance), using pre-calculated values
  server.sendContent_P(R"(<polyline fill="none" stroke="#0af" stroke-width="2" points=")");
  for (int i=0; i < 360; i += STEP){
    float X=xmap((float)i);
    float Y=ymap(dev_lut.lookup((float)i));
    snprintf(buf,sizeof(buf),"%.1f,%.1f ",X,Y);
    server.sendContent(buf);
  }
  server.sendContent_P(R"("/>)");

  server.sendContent_P(R"(</svg></div>)");

  // Deviation table every 10°, using pre-calculated values
  server.sendContent_P(R"(
    <div class="card">
    <table>
    <tr><th>Compass</th><th>Deviation</th><th></th><th>Compass</th><th>Deviation</th></tr>)");
  for (int i=10; i <= 180; i+=10){
    float v = dev_lut.lookup((float)i);
    float v2 = dev_lut.lookup((float)(i + 180));
    snprintf(buf,sizeof(buf),"<tr><td>%03d\u00B0</td><td>%+.0f\u00B0</td><td></td><td>%03d\u00B0</td><td>%+.0f\u00B0</td></tr>", i, v, i+180, v2);
    server.sendContent(buf);
  }
  server.sendContent_P(R"(</table></div>)");
  server.sendContent_P(R"(<p style="margin:20px;"><a href="/">BACK</a></p></body></html>)");
  server.sendContent("");
}

// Web UI handler for login 
void WebUIManager::handleLogin() {

  // Get client IP address
  uint32_t client_ip = server.client().remoteIP();
  
  // Check login rate limiting
  if (!this->checkLoginRateLimit(client_ip)) {
    unsigned long now = millis();
    
    // Etsi IP:n slot virheviestiä varten
    uint8_t slot = 0;
    for (uint8_t i = 0; i < MAX_IP_FOLLOWUP; i++) {
      if (login_attempts[i].ip_address == client_ip) {
        slot = i;
        break;
      }
    }
    
    unsigned long lockout_left = LOCKOUT_DURATION_MS - (now - login_attempts[slot].timestamp_ms);
    unsigned long secs_left = lockout_left / 1000;
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Too many attempts. Try again in %lu seconds", secs_left);
    
    server.send(429, "text/plain", msg);
    return;
  }

  if (!server.hasArg("password")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  
  String password = server.arg("password");
  
  // SHA
  char input_hash[65];
  this->sha256Hash(password.c_str(), input_hash);
  
  // Load stored password hash
  char stored_hash[65];
  if (!compass_prefs.loadWebPasswordHash(stored_hash)) {
    server.send(500, "text/plain", "Password not configured");
    return;
  }

  // Compare
  if (strcmp(input_hash, stored_hash) != 0) {
    display.showSuccessMessage("WEB UI LOGIN", false);
    this->recordFailedLogin(client_ip);
    this->handleLoginPage();
    return;
  }
  
  // Password correct, create session
  this->recordSuccessfulLogin(client_ip);
  char* token = this->createSession();
  
  // Set session cookie for 6 h
  char cookie[128];
  snprintf(cookie, sizeof(cookie), "session=%s; Path=/; Max-Age=21600; HttpOnly; SameSite=Lax", token);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Set-Cookie", cookie);
  server.sendHeader("Location", "/config");
  server.send(303, "text/plain", "");
  display.showSuccessMessage("WEB UI LOGIN", true);
}

// Web UI handler for login page
void WebUIManager::handleLoginPage() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html; charset=utf-8", "");
  
  server.sendContent_P(R"(
    <!DOCTYPE html><html><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=5, user-scalable=yes">
    <link rel="icon" href="data:,">
    <title>CMPS14 Login</title>
    <style>
      * { box-sizing: border-box } 
      html { font-family: Helvetica; margin: 0; padding: 0; text-align: center; }
      body { background:#000; color:#fff; max-width: 768px; margin: 0 auto; padding: 0; font-size: clamp(8px, 3vmin, 14px); font-family: Helvetica;  padding-top: 20vh; }
      h1 { font-size: clamp(12px, 5vmin, 20px); margin: 20px 0; }
      form { margin: 40px auto; max-width: 300px; }
      input[type=password] { font-size: clamp(10px, 4vmin, 16px); padding: 12px; 
                             border-radius: 6px; width: 240px; 
                             border: 1px solid #333; background: #111; 
                             color: #fff; margin: 10px 0; }
      .button { background: #00A300; border: none; color: white; 
                padding: 6px 10px; font-size: clamp(8px, 3vmin, 14px); border-radius: 6px; 
                cursor: pointer; margin: 10px 0; }
      .button:hover { background: #00C300; }
      .info { color: #888; font-size: clamp(6px, 2vmin, 10px); margin: 20px; }
    </style>
    </head>
    <body>
      <h1>CMPS14 Gateway</h1>
      <form method="POST" action="/login">
        <input type="password" name="password" placeholder="Password" required autofocus>
        <br>
        <button type="submit" class="button">LOGIN</button>
      </form>
      <div class="info">Enter password to access configuration</div>
    </body>
    </html>
  )");
  
  server.sendContent("");
}

// Web UI handler for logout
void WebUIManager::handleLogout() {
  // Empty session
  if (server.hasHeader("Cookie")) {
    String cookies = server.header("Cookie");
    char token[33];

    if (parseSessionToken(cookies.c_str(), token)) {
      // Remove session
      for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
        if (strcmp(sessions[i].token, token) == 0) {
          sessions[i].token[0] = '\0';
          break;
        }
      }
    }
  }

  // Clear cookie in browser
  server.sendHeader("Set-Cookie", "session=; Path=/; Max-Age=0");
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Logged out");
}

// Authentication
bool WebUIManager::requireAuth() {
  if (!server.hasHeader("Cookie")) {
    server.send(401, "text/plain", "Unauthorized");
    return false;
  }
  
  String cookies = server.header("Cookie");
  char token[33];
  
  if (!parseSessionToken(cookies.c_str(), token)) {
    server.send(401, "text/plain", "Unauthorized");
    return false;
  }
  
  if (!this->validateSession(token)) {
    server.send(401, "text/plain", "Session expired");
    return false;
  }
  
  return true;
}

// Check authentication without any http response
bool WebUIManager::isAuthenticated() {
  if (!server.hasHeader("Cookie")) return false;

  String cookies = server.header("Cookie");
  char token[33];

  if (!parseSessionToken(cookies.c_str(), token)) return false;

  return this->validateSession(token);
}

// Create a new session
char* WebUIManager::createSession() {
  this->cleanExpiredSessions();
  
  // Search a free slot
  for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token[0] == '\0' || 
        (millis() - sessions[i].last_seen_ms) > SESSION_TIMEOUT_MS) {
      
      // Generate 128-bit random token
      uint8_t random_bytes[16];
      esp_fill_random(random_bytes, 16); // Laitteiston RNG
      
      // Hex transformation
      for (uint8_t j = 0; j < 16; j++) {
        sprintf(&sessions[i].token[j*2], "%02x", random_bytes[j]);
      }
      sessions[i].token[32] = '\0';
      
      sessions[i].created_ms = millis();
      sessions[i].last_seen_ms = millis();
      
      return sessions[i].token;
    }
  }
  
  // No free slots, overwrite oldest
  uint8_t oldest = 0;
  for (uint8_t i = 1; i < MAX_SESSIONS; i++) {
    if (sessions[i].created_ms < sessions[oldest].created_ms) {
      oldest = i;
    }
  }
  
  // Generate new token for oldest slot
  uint8_t random_bytes[16];
  esp_fill_random(random_bytes, 16);
  
  for (uint8_t j = 0; j < 16; j++) {
    sprintf(&sessions[oldest].token[j*2], "%02x", random_bytes[j]);
  }
  sessions[oldest].token[32] = '\0';
  sessions[oldest].created_ms = millis();
  sessions[oldest].last_seen_ms = millis();
  
  return sessions[oldest].token;

}

// Validate session token
bool WebUIManager::validateSession(const char* token) {
  if (!token || strlen(token) != 32) {
    return false;
  }
  
  unsigned long now = millis();
  
  for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token[0] != '\0' && 
        strcmp(sessions[i].token, token) == 0) {
      
      // Check timeout
      if ((now - sessions[i].last_seen_ms) > SESSION_TIMEOUT_MS) {
        // Session expired
        sessions[i].token[0] = '\0';
        return false;
      }
      
      // Update last seen
      sessions[i].last_seen_ms = now;
      return true;
    }
  }
  
  return false;
}

// Clean expired sessions
void WebUIManager::cleanExpiredSessions() {
  unsigned long now = millis();
  
  for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token[0] != '\0' && 
        (now - sessions[i].last_seen_ms) > SESSION_TIMEOUT_MS) {
      sessions[i].token[0] = '\0';
    }
  }
}

// Web UI handler for password change
void WebUIManager::handleChangePassword() {
  if (!this->requireAuth()) return;

  if (!server.hasArg("old") || !server.hasArg("new") || !server.hasArg("confirm")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }

  String oldStr = server.arg("old");
  const char* old_pw = oldStr.c_str();
  String newStr = server.arg("new");
  const char* new_pw = newStr.c_str();
  String confirmStr = server.arg("confirm");
  const char* confirm_pw = confirmStr.c_str();

  // Validate
  if (strcmp(new_pw, confirm_pw) != 0) {
    server.send(400, "text/plain", "Passwords do not match");
    return;
  }

  if (strlen(new_pw) < 8) {
    server.send(400, "text/plain", "Password too short (min 8 chars)");
    return;
  }

  if (strcmp(new_pw, DEFAULT_WEB_PASSWORD) == 0) {
    server.send(400, "text/plain", "Choose a different password");
    return;
  }

  // Check old password
  char old_hash[65];
  this->sha256Hash(old_pw, old_hash);

  char stored_hash[65];
  compass_prefs.loadWebPasswordHash(stored_hash);

  if (strcmp(old_hash, stored_hash) != 0) {
    uint32_t client_ip = server.client().remoteIP();
    this->recordFailedLogin(client_ip);
    server.send(401, "text/plain", "Wrong password");
    return;
  }

  // Save new password
  char new_hash[65];
  this->sha256Hash(new_pw, new_hash);
  compass_prefs.saveWebPassword(new_hash);

  display.showSuccessMessage("PASSWORD CHANGED", true);
  server.sendHeader("Location", "/config");
  server.send(302, "text/plain", "");

}

// Web UI handler for change password page
void WebUIManager::handleChangePasswordPage() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html; charset=utf-8", "");
  
  server.sendContent_P(R"(
    <!DOCTYPE html><html><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=5, user-scalable=yes">
    <link rel="icon" href="data:,">
    <title>Change Password</title>
    <style>
      * { box-sizing: border-box } 
      html { font-family: Helvetica; margin: 0; padding: 0; text-align: center; }
      body { background:#000; color:#fff; max-width: 768px; margin: 0 auto; padding: 0; font-size: clamp(8px, 3vmin, 14px); }
      .button { background-color: #00A300; border: none; color: white; 
                padding: 6px 10px; text-decoration: none; font-size: clamp(8px, 3vmin, 14px); 
                margin: 8px; cursor: pointer; border-radius:6px; }
      .button2 { background-color: #D10000; }
      .card { width:92%; margin:10px auto; padding:10px; background:#0b0b0b; 
              border-radius:6px; box-shadow:0 0 0 1px #222 inset; }
      h2 { margin:16px 0; font-size: clamp(10px, 4vmin, 16px); }
      label { display:block; text-align:left; margin:8px 0 4px 0; }
      input[type=password] { font-size: clamp(8px, 3vmin, 14px); width: 90%; padding:8px; 
                             margin:4px 0; border-radius:6px; border:1px solid #333; 
                             background:#111; color:#fff; }
    </style>
    </head>
    <body>
      <h2><a href="/config" style="color:white; text-decoration:none;">CHANGE PASSWORD</a></h2>
      <div class='card'>
        <form method="POST" action="/changepassword">
          <label>Current password:</label>
          <input type="password" name="old" required>
          <label>New password (min 8 chars):</label>
          <input type="password" name="new" required minlength="8">
          <label>Confirm new password:</label>
          <input type="password" name="confirm" required>
          <br><br>
          <button type="submit" class="button">SAVE</button>
          <a href="/config"><button type="button" class="button button2">CANCEL</button></a>
        </form>
      </div>
    </body>
    </html>
  )");
  
  server.sendContent("");
}

// Cookie parser helper
bool WebUIManager::parseSessionToken(const char* cookies, char* token_out_33bytes) {
  if (!cookies) return false;
  
  const char* session_start = strstr(cookies, "session=");
  if (!session_start) return false;
  
  session_start += 8;  // Skip "session="
  
  const char* session_end = strchr(session_start, ';');
  size_t token_len;
  if (session_end) {
    token_len = session_end - session_start;
  } else {
    token_len = strlen(session_start);
  }
  
  if (token_len > 32) token_len = 32;
  strncpy(token_out_33bytes, session_start, token_len);
  token_out_33bytes[token_len] = '\0';
  
  // Trim whitespace
  char* trimmed = token_out_33bytes;
  while (*trimmed == ' ' || *trimmed == '\t') {
    memmove(token_out_33bytes, trimmed + 1, strlen(trimmed));
    trimmed = token_out_33bytes;
  }
  
  char* end = token_out_33bytes + strlen(token_out_33bytes) - 1;
  while (end > token_out_33bytes && (*end == ' ' || *end == '\t')) {
    *end = '\0';
    end--;
  }
  
  return strlen(token_out_33bytes) == 32;  // Valid token is exactly 32 chars
}

// SHA256 hash helper
void WebUIManager::sha256Hash(const char* input, char* output_hex_64bytes) {
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char*)input, strlen(input));
  
  unsigned char hash[32];
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);
  
  // Convert to hex string
  for (int i = 0; i < 32; i++) {
    sprintf(&output_hex_64bytes[i * 2], "%02x", hash[i]);
  }
  output_hex_64bytes[64] = '\0';
}

// Check if an IP address is subject to rate limiting
bool WebUIManager::checkLoginRateLimit(uint32_t client_ip) {
  this->cleanOldLoginAttempts();
  
  unsigned long now = millis();
  
  // Check for IP address if already followed up
  for (uint8_t i = 0; i < MAX_IP_FOLLOWUP; i++) {
    if (login_attempts[i].ip_address == client_ip) {
      // Found, check if subject to lockout
      if (login_attempts[i].count >= MAX_LOGIN_ATTEMPTS) {
        if ((now - login_attempts[i].timestamp_ms) < LOCKOUT_DURATION_MS) {
          return false; // Blocked
        }
      }
      return true; // Found IP, allowed
    }
  }
  
  // IP not found, allowed
  return true;
}

// Capture failed login attempt from a client IP address
void WebUIManager::recordFailedLogin(uint32_t client_ip) {
  // Check for IP
  for (uint8_t i = 0; i < MAX_IP_FOLLOWUP; i++) {
    if (login_attempts[i].ip_address == client_ip) {
      // Update existing
      login_attempts[i].timestamp_ms = millis();
      login_attempts[i].count++;
      return;
    }
  }
  
  // IP not found, find a slot
  int8_t empty_slot = -1;
  int8_t oldest_slot = 0;
  
  for (uint8_t i = 0; i < MAX_IP_FOLLOWUP; i++) {
    if (login_attempts[i].ip_address == 0 || login_attempts[i].count == 0) {
      empty_slot = i;
      break;
    }
    if (login_attempts[i].timestamp_ms < login_attempts[oldest_slot].timestamp_ms) {
      oldest_slot = i;
    }
  }
  
  uint8_t slot = (empty_slot >= 0) ? empty_slot : oldest_slot;
  
  login_attempts[slot].ip_address = client_ip;
  login_attempts[slot].timestamp_ms = millis();
  login_attempts[slot].count = 1;
}

// Capture successful login from a client IP address
void WebUIManager::recordSuccessfulLogin(uint32_t client_ip) {
  // Search and reset this client IP
  for (uint8_t i = 0; i < MAX_IP_FOLLOWUP; i++) {
    if (login_attempts[i].ip_address == client_ip) {
      login_attempts[i].ip_address = 0;
      login_attempts[i].count = 0;
      login_attempts[i].timestamp_ms = 0;
      return;
    }
  }
}

// Reset the outdated attempts
void WebUIManager::cleanOldLoginAttempts() {
  unsigned long now = millis();
  
  for (uint8_t i = 0; i < MAX_IP_FOLLOWUP; i++) {
    if (login_attempts[i].count > 0 &&
        (now - login_attempts[i].timestamp_ms) > THROTTLE_WINDOW_MS) {
      if (login_attempts[i].count < MAX_LOGIN_ATTEMPTS) {
        login_attempts[i].ip_address = 0;
        login_attempts[i].count = 0;
        login_attempts[i].timestamp_ms = 0;
      }
    }
  }
}

// Web UI handler for software restart of ESP32
void WebUIManager::handleRestart() {
  uint32_t ms = 2999;
  if (server.hasArg("ms")){
    long v = server.arg("ms").toInt();
    if (v >= ms && v < 20000) ms = (uint32_t)v;
  }

  char line2[17];
  snprintf(line2, sizeof(line2), "%5lu MS", (unsigned long)ms);
  display.showInfoMessage("RESTARTING IN", line2);

  // Draw HTML page which refreshes to root config page in 30 seconds
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);            
  server.sendHeader("Connection", "close");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent_P(R"(
    <!DOCTYPE html><html><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=5, user-scalable=yes">
    <meta http-equiv="refresh" content="20; url=/">
    <style>
      * { box-sizing: border-box } 
      html { font-family: Helvetica; margin: 0; padding: 0; text-align: center; }
      body { background:#000; color:#fff; max-width: 768px; margin:18vh 0 0 0; padding: 0; font-size: clamp(8px, 3vmin, 14px); }
      .msg{font-size: clamp(12px, 5vmin, 20px);}
      p{color:#bbb}
    </style>
    <script>
      setTimeout(function() { location.replace("/"); }, 21000);
    </script>
    </head><body>
      <div class="msg">RESTARTING...</div>
      <p>Please wait.</p>
      <p>This page will refresh in 20 seconds.</p>
    </body></html>
  )");
  server.sendContent("");

  delay(300);

  WiFiClient client = server.client();
  if (client) client.stop();

  if(signalk.isOpen()) {
    signalk.closeWebsocket();
  }

  delay(ms);
  ESP.restart();
}


