#include "WebUIManager.h"


// === P U B L I C ===

WebUIManager::WebUIManager(
    CMPS14Processor &compassref,
    CMPS14Preferences &compass_prefsref,
    SignalKBroker &signalkref,
    DisplayManager &displayref
    ) : server(80),
        compass(compassref),
        compass_prefs(compass_prefsref), 
        signalk(signalkref),
        display(displayref) {}

// Init the webserver
void WebUIManager::begin() {
  this->setupRoutes();
  server.begin();
}

// Handle client request
void WebUIManager::handleRequest() {
  server.handleClient();
}

// === P R I V A T E ===

// Set the handlers for webserver endpoints
void WebUIManager::setupRoutes() {
  server.on("/", HTTP_GET, [this]() {
        this->handleRoot();
    });
  server.on("/status", HTTP_GET, [this]() {
        this->handleStatus();
    });
  server.on("/cal/on", HTTP_GET, [this]() {
        this->handleStartCalibration();
    });
  server.on("/cal/off", HTTP_GET, [this]() {
        this->handleStopCalibration();
    });
  server.on("/store/on", HTTP_GET, [this]() {
        this->handleSaveCalibration();
    });
  server.on("/reset/on", HTTP_GET, [this]() {
        this->handleReset();
    });
  server.on("/offset/set", HTTP_GET, [this]() {
        this->handleSetOffset();
    });
  server.on("/dev8/set", HTTP_GET, [this]() {
        this->handleSetDeviations();
    });
  server.on("/calmode/set", HTTP_GET, [this]() {
        this->handleSetCalmode();
    });
  server.on("/magvar/set", HTTP_GET, [this]() {
        this->handleSetMagvar();
    });
  server.on("/heading/mode", HTTP_GET, [this]() {
        this->handleSetHeadingMode();
    });
  server.on("/restart", HTTP_GET, [this]() {
        this->handleRestart();
    });
  server.on("/deviationdetails", HTTP_GET, [this]() {
        this->handleDeviationTable();
    });
  server.on("/level", HTTP_GET, [this]() {
        this->handleLevel();
    });
}

// Web UI handler for CALIBRATE button
void WebUIManager::handleStartCalibration(){
  if (compass.startCalibration(CAL_MANUAL)) {
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

  if (WiFi.isConnected()) {
    display.setWifiInfo(WiFi.RSSI(), WiFi.localIP());
  } 

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

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  char out[1024];
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
    CalMode v = CAL_USE;
    if (c == '0') v = CAL_FULL_AUTO;
    else if (c == '1') v = CAL_SEMI_AUTO;
    else if (c == '2') v = CAL_MANUAL;
    else v = CAL_USE;
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
    <!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><link rel="icon" href="data:,"><style>
    html { font-family: Helvetica; display: inline-block; margin: 0 auto; text-align: center;}
    body { background:#000; color:#fff; }
    .button { background-color: #00A300; border: none; color: white; padding: 6px 20px; text-decoration: none; font-size: 3vmin; max-font-size: 24px; min-font-size: 10px; margin: 2px; cursor: pointer; border-radius:6px; text-align:center}
    .button2 { background-color: #D10000; }
    .button:disabled, .button2:disabled { opacity:0.5; cursor:not-allowed; }
    .card { width:92%; margin:2px auto; padding:4px; background:#0b0b0b; border-radius:6px; box-shadow:0 0 0 1px #222 inset; }
    h1 { margin:12px 0 8px 0; } h2 { margin:8px 0; font-size: 4vmin; max-font-size: 16px; min-font-size: 10px; } h3 { margin:6px 0; font-size: 3vmin; max-font-size: 14px; min-font-size: 8px; }
    label { display:inline-block; min-width:40px; text-align:right; margin-right:6px; }
    input[type=number]{ font-size: 3vmin; max-font-size: 14px; min-font-size: 8px; width:60px; padding:4px 6px; margin:4px; border-radius:6px; border:1px solid #333; background:#111; color:#fff; }
    #st { font-size: 2vmin; max-font-size: 18px; min-font-size: 6px; line-height: 1.2; color: #DBDBDB; background-color: #000; padding: 8px; border-radius: 8px; width: 90%; margin: auto; text-align: center; white-space: pre-line; font-family: monospace;}
    </style></head><body>
    <h2><a href="/" style="color:white; text-decoration:none;">CMPS14 CONFIG</a></h2>
    )");

  // DIV Calibrate, Stop, Reset
  server.sendContent_P(R"(
    <div class='card' id='controls'>)");
  if (mode_runtime == CAL_FULL_AUTO) {
    snprintf(buf, sizeof(buf), "Current mode: %s (%s)<br>", calModeToString(mode_runtime), this->ms_to_hms_str(compass.getFullAutoLeft()));
    server.sendContent(buf);
  } else {
    snprintf(buf, sizeof(buf), "Current mode: %s<br>", calModeToString(mode_runtime));
    server.sendContent(buf);
  }

  if (mode_runtime == CAL_SEMI_AUTO || mode_runtime == CAL_MANUAL) {
    server.sendContent_P(R"(<a href="/cal/off"><button class="button button2">STOP</button></a>)");
    if (!compass.isCalProfileStored()) {
      server.sendContent_P(R"(<a href="/store/on"><button class="button">SAVE</button></a>)");
    } else {
      server.sendContent_P(R"(<a href="/store/on"><button class="button button2">REPLACE</button></a>)");
    }
  } else if (mode_runtime == CAL_USE) {
    server.sendContent_P(R"(<a href="/cal/on"><button class="button">CALIBRATE</button></a>)");
  } else if (mode_runtime == CAL_FULL_AUTO) {
    server.sendContent_P(R"(<a href="/cal/off"><button class="button button2">STOP</button></a>)");
  }
  server.sendContent_P(R"(<a href="/reset/on"><button class="button button2">RESET</button></a></div>)");

  // DIV Calibration mode on boot
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/calmode/set" method="get">
    <label>Boot mode </label><label><input type="radio" name="c" value="0")");
  if (mode_boot == CAL_FULL_AUTO) server.sendContent_P(R"( checked)");
  server.sendContent_P(R"(>Full auto </label><label>
    <input type="radio" name="c" value="1")");
  if (mode_boot == CAL_SEMI_AUTO) server.sendContent_P(R"( checked)");
  server.sendContent_P(R"(>Auto </label><label>
    <input type="radio" name="c" value="3")");
  if (mode_boot == CAL_USE) server.sendContent_P(R"( checked)");
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
        fetch('/status').then(r=>r.json()).then(j=>{
          const d=[
            'Installation offset: '+fmt0(j.offset)+'\u00B0',
            'Heading (C): '+fmt0(j.compass_deg)+'\u00B0',
            'Deviation: '+fmt0(j.dev)+'\u00B0',
            'Heading (M): '+fmt0(j.hdg_deg)+'\u00B0',
            'Variation: '+fmt0(j.variation)+'\u00B0',
            'Heading (T): '+fmt0(j.heading_true_deg)+'\u00B0',
            'Pitch: '+fmt1(j.pitch_deg)+'\u00B0'+' Roll: '+fmt1(j.roll_deg)+'\u00B0',
            'Pitch leveling: '+fmt1(j.pitch_level)+'\u00B0'+' Roll leveling: '+fmt1(j.roll_level)+'\u00B0',
            'Acc: '+j.acc+', Mag: '+j.mag+', Sys: '+j.sys,
            'HcA: '+fmt1(j.hca)+', HcB: '+fmt1(j.hcb)+', HcC: '+fmt1(j.hcc)+', HcD: '+fmt1(j.hcd)+', HcE: '+fmt1(j.hce),
            'WiFi: '+j.wifi+' ('+j.rssi+')'
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
  server.sendContent_P(R"(
    <div class='card'>
    <a href="/level"><button class="button">LEVEL CMPS14</button></a>
    <a href="/restart?ms=5003"><button class="button button2">RESTART ESP32</button></a></div>
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
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel="icon" href="data:,">
    <title>Deviation details</title>
    <style>
      body{font-size:3vmin;max-font-size:14px;min-font-size:8px;background:#000;color:#fff;font-family:Helvetica;margin:0 auto;text-align:center}
      .card{font-size:3vmin;max-font-size:14px;min-font-size:8px;width:92%;margin:8px auto;padding:8px;background:#0b0b0b;border-radius:6px;box-shadow:0 0 0 1px #222 inset}
      table{font-size:3vmin;max-font-size:14px;min-font-size:8px;margin:8px auto;border-collapse:collapse;color:#ddd}
      td,th{font-size:3vmin;max-font-size:14px;min-font-size:8px;border:1px solid #333;padding:4px 8px}
      h2{margin:8px 0; font-size: 4vmin; max-font-size: 16px; min-font-size: 10px;}
      a{color:#fff;text-decoration:none;}
    </style>
    </head><body>
    <div class="card">
  )");

  // SVG settings
  const int W=800, H=400;
  const float xpad=40, ypad=20;
  const float xmin=0, xmax=360;
  const int STEP = 10; // Sample every 10°
  const int NUMOF_SAMPLES = (360 / STEP) + 1;

  // Pre-calculate all deviation values at 10° resolution
  float deviations[NUMOF_SAMPLES];
  HarmonicCoeffs hc = compass.getHarmonicCoeffs();
  for (int i=0; i < NUMOF_SAMPLES; i++) {
    deviations[i] = computeDeviation(hc, (float)(i * STEP));
  }

  // Use pre-calclulated deviations
  float ymax = 0.0f;
  for (int i=0; i < NUMOF_SAMPLES; i++){
    if (fabs(deviations[i]) > ymax) ymax = fabs(deviations[i]);
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
      "<text x=\"%.1f\" y=\"%.1f\" fill=\"#aaa\" font-size=\"12\" text-anchor=\"middle\">%03d</text>",
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
      "<text x=\"%.1f\" y=\"%.1f\" fill=\"#aaa\" font-size=\"12\" text-anchor=\"end\">%+d°</text>",
      xmap(xmin)-6, Y+4, j);
    server.sendContent(buf);
  }

  // Polyline (every 10 degrees for performance), using pre-calculated values
  server.sendContent_P(R"(<polyline fill="none" stroke="#0af" stroke-width="2" points=")");
  for (int i=0; i < NUMOF_SAMPLES; i++){
    float X=xmap((float)(i * STEP));
    float Y=ymap(deviations[i]);
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
    int idx1 = i / STEP;
    int idx2 = (i + 180) / STEP;
    float v = deviations[idx1];
    float v2 = deviations[idx2];
    snprintf(buf,sizeof(buf),"<tr><td>%03d\u00B0</td><td>%+.0f\u00B0</td><td></td><td>%03d\u00B0</td><td>%+.0f\u00B0</td></tr>", i, v, i+180, v2);
    server.sendContent(buf);
  }
  server.sendContent_P(R"(</table></div>)");
  server.sendContent_P(R"(<p style="margin:20px;"><a href="/">BACK</a></p></body></html>)");
  server.sendContent("");
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
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta http-equiv="refresh" content="30; url=/">
    <style>
      body{background:#000;color:#fff;font-family:Helvetica;text-align:center;margin:18vh 0 0 0}
      .msg{font-size:5vmin;max-font-size:24px;min-font-size:12px}
      p{color:#bbb}
    </style>
    <script>
      setTimeout(function() { location.replace("/"); }, 31000);
    </script>
    </head><body>
      <div class="msg">RESTARTING...</div>
      <p>Please wait.</p>
      <p>This page will refresh in 30 seconds.</p>
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


