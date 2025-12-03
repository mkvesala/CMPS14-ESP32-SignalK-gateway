#include "webui.h"

// Set up webserver to call the handlers
void setupWebserverCallbacks() {
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/cal/on", handleStartCalibration);
  server.on("/cal/off", handleStopCalibration);
  server.on("/store/on", handleSaveCalibration);
  server.on("/reset/on", handleReset);
  server.on("/offset/set", handleSetOffset);
  server.on("/dev8/set", handleSetDeviations);
  server.on("/calmode/set", handleSetCalmode);
  server.on("/magvar/set", handleSetMagvar);
  server.on("/heading/mode", handleSetHeadingMode);
  server.on("/restart", handleRestart);
  server.on("/deviationdetails", handleDeviationTable);
  server.begin();
}

// Web UI handler for CALIBRATE button
void handleStartCalibration(){
  if (compass.startCalibration(CAL_MANUAL)) {
    // ok
  }
  handleRoot();
}

// Web UI handler for STOP button
void handleStopCalibration(){
  if (compass.stopCalibration()) {
    // ok
  }
  handleRoot();
}

// Web UI handler for SAVE button
void handleSaveCalibration(){
  if (compass.saveCalibrationProfile()) {
    // ok
  }
  handleRoot();
}

// Web UI handler for RESET button
void handleReset(){
  if (compass.reset()) {
    // ok
  }
  handleRoot(); 
}

// Web UI handler for status block, build json with appropriate data
void handleStatus() {
  
  uint8_t mag = 255, acc = 255, gyr = 255, sys = 255;
  uint8_t statuses[4];
  compass.getCalStatus(statuses);
  mag = statuses[0];
  acc = statuses[1];
  gyr = statuses[2];
  sys = statuses[3];

  if (WiFi.isConnected()) {
    setIPAddrCstr();
    setRSSICstr();
  } 

  HarmonicCoeffs hc = compass.getHarmonicCoeffs();
  StaticJsonDocument<1024> doc;

  doc["cal_mode"]             = calModeToString(compass.getCalibrationModeRuntime());
  doc["cal_mode_boot"]        = calModeToString(compass.getCalibrationModeBoot());
  doc["fa_left"]              = ms_to_hms_str(full_auto_left_ms);
  doc["wifi"]                 = IPc;
  doc["rssi"]                 = RSSIc;
  doc["hdg_deg"]              = compass.getHeadingDeg();
  doc["compass_deg"]          = compass.getCompassDeg();
  doc["pitch_deg"]            = compass.getPitchDeg();
  doc["roll_deg"]             = compass.getRollDeg();
  doc["offset"]               = compass.getInstallationOffset();
  doc["dev"]                  = compass.getDeviation();
  doc["variation"]            = compass.getVariation();
  doc["heading_true_deg"]     = compass.getHeadingTrueDeg();
  doc["acc"]                  = acc;
  doc["mag"]                  = mag;
  doc["sys"]                  = sys;
  doc["hca"]                  = hc.A;
  doc["hcb"]                  = hc.B;
  doc["hcc"]                  = hc.C;
  doc["hcd"]                  = hc.D;
  doc["hce"]                  = hc.E;
  doc["use_manual_magvar"]    = compass.isUsingManualVariation();   
  doc["send_hdg_true"]        = send_hdg_true;         
  doc["stored"]               = compass.isCalProfileStored(); 

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  char out[1024];
  size_t n = serializeJson(doc, out, sizeof(out));
  server.send(200, "application/json; charset=utf-8", out);
}

// Web UI handler for installation offset, to correct raw compass heading
void handleSetOffset() {
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (!validf(v)) v = 0.0f;
    if (v < -180.0f) v = -180.0f;
    if (v >  180.0f) v =  180.0f;

    compass.setInstallationOffset(v);

    // Save prefrences permanently
    prefs.begin("cmps14", false);
    prefs.putFloat("offset_deg", v);
    prefs.end();

    char line2[17];
    snprintf(line2, sizeof(line2), "SAVED %5.0f%c", v, 223);
    updateLCD("INSTALL OFFSET", line2, true);
  }
  handleRoot();
}

// Web UI handler for 8 measured deviation values, to correct raw compass heading --> navigation.headingMagnetic
void handleSetDeviations() {
  auto getf = [&](const char* k) -> float {
    if (!server.hasArg(k)) return 0.0f;
    float v = server.arg(k).toFloat();
    if (!validf(v)) v = 0.0f;
    if (v < -90.0f) v = -90.0f;
    if (v >  90.0f) v =  90.0f;
    return v;
  };

  dev_at_card_deg[0] = getf("N");
  dev_at_card_deg[1] = getf("NE");
  dev_at_card_deg[2] = getf("E");
  dev_at_card_deg[3] = getf("SE");
  dev_at_card_deg[4] = getf("S");
  dev_at_card_deg[5] = getf("SW");
  dev_at_card_deg[6] = getf("W");
  dev_at_card_deg[7] = getf("NW");

  // Calculate 5 coeffs
  HarmonicCoeffs hc = computeHarmonicCoeffs(dev_at_card_deg); 
  compass.setHarmonicCoeffs(hc);

  prefs.begin("cmps14", false);
  for (int i = 0; i < 8; i++) {
    prefs.putFloat((String("dev") + String(i)).c_str(), dev_at_card_deg[i]);
  }
  prefs.putFloat("hc_A", hc.A);
  prefs.putFloat("hc_B", hc.B);
  prefs.putFloat("hc_C", hc.C);
  prefs.putFloat("hc_D", hc.D);
  prefs.putFloat("hc_E", hc.E);
  prefs.end();

  updateLCD("DEVIATION TABLE", "SAVED", true);

  handleRoot();
}

// Web UI handler to choose calibration mode on boot
void handleSetCalmode() {
  if (server.hasArg("calmode") && server.hasArg("fastop")) {
    
    String m = server.arg("calmode");
    CalMode v = CAL_USE;
    if (m == "full")         v = CAL_FULL_AUTO;
    else if (m == "semi")    v = CAL_SEMI_AUTO;
    else if (m == "manual")  v = CAL_MANUAL;
    else                     v = CAL_USE;
    compass.setCalibrationModeBoot(v);
   
    if (!(compass.getCalibrationModeRuntime() == CAL_FULL_AUTO)) {
      long to = server.arg("fastop").toInt();
      if (to <= 0) to = 0;
      if (to > 60) to = 60;
      full_auto_stop_ms = 60 * 1000 * to;
      prefs.begin("cmps14", false);
      prefs.putULong("fastop", (uint32_t)full_auto_stop_ms);
      prefs.end();
    }

    prefs.begin("cmps14", false);
    prefs.putUChar("cal_mode_boot", (uint8_t)v);
    prefs.end();
    updateLCD("BOOT MODE SAVED", calModeToString(v), true);
  }
  handleRoot();
}

// Web UI handler to set magnetic variation manually.
void handleSetMagvar() {
  
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (!validf(v)) v = 0.0f;
    if (v < -90.0f) v = -90.0f;
    if (v >  90.0f) v =  90.0f;
    compass.setManualVariation(v);

    prefs.begin("cmps14", false);
    prefs.putFloat("mv_man_deg", v);
    prefs.end();

    char line2[17];
    snprintf(line2, sizeof(line2), "SAVED %5.0f%c %c", fabs(v), 223, (v >= 0 ? 'E':'W'));
    updateLCD("MAG VARIATION", line2, true);
  }
  handleRoot();
}

// Web UI handler to set heading mode TRUE or MAGNETIC
void handleSetHeadingMode() {
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    send_hdg_true = (mode == "true");
  }

  prefs.begin("cmps14", false);
  prefs.putBool("send_hdg_true", send_hdg_true);
  prefs.end();

  updateLCD("HDG MODE SAVED", send_hdg_true ? "TRUE" : "MAGNETIC", true);
  handleRoot();
}

// Web UI handler for the configuration HTML page 
void handleRoot() {

  CalMode mode_runtime = compass.getCalibrationModeRuntime();
  CalMode mode_boot = compass.getCalibrationModeBoot();

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
    char buf[128];
    snprintf(buf, sizeof(buf), "Current mode: %s (%s)<br>", calModeToString(mode_runtime), ms_to_hms_str(full_auto_left_ms));
    server.sendContent(buf);
  } else {
    char buf[64];
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
    <label>Boot mode </label><label><input type="radio" name="calmode" value="full")");
  { if (mode_boot == CAL_FULL_AUTO) server.sendContent_P(R"( checked)"); }
  server.sendContent_P(R"(>Full auto </label><label>
    <input type="radio" name="calmode" value="semi")");
  { if (mode_boot == CAL_SEMI_AUTO) server.sendContent_P(R"( checked)"); }
  server.sendContent_P(R"(>Auto </label><label>
    <input type="radio" name="calmode" value="use")");
  { if (mode_boot == CAL_USE) server.sendContent_P(R"( checked)"); }
  server.sendContent_P(R"(>Use/Manual</label><br>
    <label>Full auto stops in </label>
    <input type="number" name="fastop" step="1" min="0" max="60" value=")");
      {
        char buf[32];
        float to = (float)(full_auto_stop_ms/1000/60);
        snprintf(buf, sizeof(buf), "%.0f", to);
        server.sendContent(buf);
      }
  server.sendContent_P(R"("> mins (0 never)<br><input type="submit" id="calmodebtn" class="button" value="SAVE"></form></div>)");

  // DIV Set installation offset
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/offset/set" method="get">
    <label>Installation offset</label>
    <input type="number" name="v" step="1" min="-180" max="180" value=")");
      {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", compass.getInstallationOffset());
        server.sendContent(buf);
      }
  server.sendContent_P(R"(">&deg; <input type="submit" value="SAVE" class="button"></form></div>)");

  // DIV Set deviation 
  server.sendContent_P(R"(
    <div class='card'>Measured deviations<form action="/dev8/set" method="get"><div>)");

  // Row 1: N NE
  {
    char row1[256];
    snprintf(row1, sizeof(row1),
      "<label>N</label><input name=\"N\"  type=\"number\" step=\"1\" value=\"%.0f\">&deg; "
      "<label>NE</label><input name=\"NE\" type=\"number\" step=\"1\" value=\"%.0f\">&deg; ",
      dev_at_card_deg[0], dev_at_card_deg[1]);
    server.sendContent(row1);
  }
  server.sendContent_P(R"(</div><div>)");

  // Row 2:  E SE
  {
  char row2[256];
    snprintf(row2, sizeof(row2),
      "<label>E</label><input name=\"E\"  type=\"number\" step=\"1\" value=\"%.0f\">&deg; "
      "<label>SE</label><input name=\"SE\" type=\"number\" step=\"1\" value=\"%.0f\">&deg; ",
      dev_at_card_deg[2], dev_at_card_deg[3]);
    server.sendContent(row2);
  }
  server.sendContent_P(R"(</div><div>)");

  // Row 3: S SW
  {
  char row3[256];
    snprintf(row3, sizeof(row3),
      "<label>S</label><input name=\"S\"  type=\"number\" step=\"1\" value=\"%.0f\">&deg; "
      "<label>SW</label><input name=\"SW\" type=\"number\" step=\"1\" value=\"%.0f\">&deg; ",
      dev_at_card_deg[4], dev_at_card_deg[5]);
    server.sendContent(row3);
  }
  server.sendContent_P(R"(</div><div>)");

  // Row 4: W NW
  {
    char row4[256];
    snprintf(row4, sizeof(row4),
      "<label>W</label><input name=\"W\"  type=\"number\" step=\"1\" value=\"%.0f\">&deg; "
      "<label>NW</label><input name=\"NW\" type=\"number\" step=\"1\" value=\"%.0f\">&deg; ",
      dev_at_card_deg[6], dev_at_card_deg[7]);
    server.sendContent(row4);
  }

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
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.0f", compass.getManualVariation());
      server.sendContent(buf);
    }
  server.sendContent_P(R"(">&deg; <input type="submit" value="SAVE" class="button"></form></div>)");

  // DIV Set heading mode TRUE or MAGNETIC
  server.sendContent_P(R"(
    <div class='card'>
    <form action="/heading/mode" method="get">
    <label>Heading </label><label><input type="radio" name="mode" value="true")");
    { if (send_hdg_true) server.sendContent_P(R"( checked)"); }
    server.sendContent_P(R"(>True</label><label>
    <input type="radio" name="mode" value="mag")");
    { if (!send_hdg_true) server.sendContent_P(R"( checked)"); }
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
    <a href="/restart?ms=5003"><button class="button button2">RESTART ESP32</button></a></div>
    </body>
    </html>)");
  server.sendContent("");
}

// WebUI handler to draw deviation table and deviation curve
void handleDeviationTable(){
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
  float ymax = 0.0f;
  HarmonicCoeffs hc = compass.getHarmonicCoeffs();
  for (int d=0; d<=360; ++d){
    float v = computeDeviation(hc, (float)d);
    if (fabs(v) > ymax) ymax = fabs(v);
  }
  ymax = max(ymax + 1.0f, 5.0f); 

  auto xmap = [&](float x){ return xpad + (x - xmin) * ( (W-2*xpad) / (xmax-xmin) ); };
  auto ymap = [&](float y){ return H-ypad - (y + ymax) * ( (H-2*ypad) / (2*ymax) ); };

  // Grid
  char buf[160];
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

  // Polyline
  server.sendContent_P(R"(<polyline fill="none" stroke="#0af" stroke-width="2" points=")");
  for (int d=0; d<=360; ++d){
    float X=xmap((float)d);
    float Y=ymap(computeDeviation(hc,(float)d));
    snprintf(buf,sizeof(buf),"%.1f,%.1f ",X,Y);
    server.sendContent(buf);
  }
  server.sendContent_P(R"("/>)");

  server.sendContent_P(R"(</svg></div>)");

  // Deviation table every 10°
  server.sendContent_P(R"(
    <div class="card">
    <table>
    <tr><th>Compass</th><th>Deviation</th><th></th><th>Compass</th><th>Deviation</th></tr>)");
  for (int d=10; d<=180; d+=10){
    float v = computeDeviation(hc, (float)d);
    int d2 = d+180;
    float v2 = computeDeviation(hc, (float)d2);
    snprintf(buf,sizeof(buf),"<tr><td>%03d\u00B0</td><td>%+.0f\u00B0</td><td></td><td>%03d\u00B0</td><td>%+.0f\u00B0</td></tr>", d, v, d2, v2);
    server.sendContent(buf);
  }
  server.sendContent_P(R"(</table></div>)");
  server.sendContent_P(R"(<p style="margin:20px;"><a href="/">BACK</a></p></body></html>)");
  server.sendContent("");
}

// Web UI handler for software restart of ESP32
void handleRestart() {
  uint32_t ms = 2999;
  if (server.hasArg("ms")){
    long v = server.arg("ms").toInt();
    if (v >= ms && v < 20000) ms = (uint32_t)v;
  }

  char line2[17];
  snprintf(line2, sizeof(line2), "%5lu ms", (unsigned long)ms);
  updateLCD("RESTARTING IN", line2, true);

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

  if(ws_open) {
    ws.close();
    ws_open = false;
  }

  delay(ms);
  ESP.restart();
}
