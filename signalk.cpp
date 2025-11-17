#include "signalk.h"

// Create SignalK server URL
void build_sk_url() {
  if (strlen(SK_TOKEN) > 0)
    snprintf(SK_URL, sizeof(SK_URL), "ws://%s:%d/signalk/v1/stream?token=%s", SK_HOST, SK_PORT, SK_TOKEN);
  else
    snprintf(SK_URL, sizeof(SK_URL), "ws://%s:%d/signalk/v1/stream", SK_HOST, SK_PORT);
}

// Set SignalK source and OTA hostname (equal) based on ESP32 MAC address tail
void make_source_from_mac() {
  uint8_t m[6];
  WiFi.macAddress(m);
  snprintf(SK_SOURCE, sizeof(SK_SOURCE), "esp32.cmps14-%02x%02x%02x", m[3], m[4], m[5]);
}

// Description for WiFi signal level
void classify_rssi(int rssi) {
  const char* label =
      (rssi > -55) ? "EXCELLENT" :
      (rssi < -80) ? "POOR" : "OK";
  strncpy(RSSIc, label, sizeof(RSSIc) - 1);
  RSSIc[sizeof(RSSIc) - 1] = '\0';
}

// Websocket callbacks
void setup_ws_callbacks() {
  
  ws.onEvent([](WebsocketsEvent e, String){
    if (e == WebsocketsEvent::ConnectionOpened) {
      ws_open = true;
      if (!send_hdg_true) return;                             // Do nothing if user has switched off sending of navigation.headingTrue
      StaticJsonDocument<256> sub;                            // Otherwise, subscribe navigation.magneticVariation path from SignalK server
      sub["context"] = "vessels.self";
      auto subscribe = sub.createNestedArray("subscribe");
      auto s = subscribe.createNestedObject();
      s["path"] = "navigation.magneticVariation";
      s["format"] = "delta";
      s["policy"] = "ideal";
      s["period"] = 1000;                                     // Request ~1 Hz updates

      char buf[256];
      size_t n = serializeJson(sub, buf, sizeof(buf));
      ws.send(buf, n);
    }
    if (e == WebsocketsEvent::ConnectionClosed)  { ws_open = false; }
    if (e == WebsocketsEvent::GotPing)           { ws.pong(); }
  });

  ws.onMessage([](WebsocketsMessage msg){
    if (!send_hdg_true) return;                                // Do nothing if user has switched off sending of navigation.headingTrue and if data is not valid / found
    if (!msg.isText()) return;
    StaticJsonDocument<1024> d;
    if (deserializeJson(d, msg.data())) return;
    if (!d.containsKey("updates")) return;
    for (JsonObject up : d["updates"].as<JsonArray>()) {
      if (!up.containsKey("values")) continue;
      for (JsonObject v : up["values"].as<JsonArray>()) {
        if (!v.containsKey("path")) continue;
        const char* path = v["path"];
        if (!path) continue;
        if (strcmp(path, "navigation.magneticVariation") == 0) {
          if (v["value"].is<float>() || v["value"].is<double>()) {  
            float mv = v["value"].as<float>();
            if (validf(mv)) {                                  // If received valid value, set global variation value to this and stop using manual value
              magvar_rad = mv;
              use_manual_magvar = false;
              magvar_deg = magvar_rad * RAD_TO_DEG;
            } else use_manual_magvar = true;
          }
        }
      }
    }
  });

}

// Send batch of SignalK deltas but only if change exceeds the deadband limits (no unnecessary sending)
void send_batch_delta_if_needed() {
  
  if (LCD_ONLY || !ws_open) return;                                                                    // execute only if WiFi and Websocket ok
  if (!validf(heading_rad) || !validf(pitch_rad) || !validf(roll_rad)) return;                         // execute only if values are valid

  static unsigned long last_tx_ms = 0;
  const unsigned long now = millis();
  if (now - last_tx_ms < MIN_TX_INTERVAL_MS) return;                                                   // Timer for sending to SignalK

  static float last_h = NAN, last_p = NAN, last_r = NAN;
  bool changed_h = false, changed_p = false, changed_r = false;

  if (!validf(last_h) || fabsf(ang_diff_rad(heading_rad, last_h)) >= DB_HDG_RAD) {
    changed_h = true; last_h = heading_rad;
  }
  if (!validf(last_p) || fabsf(pitch_rad - last_p) >= DB_ATT_RAD) {
    changed_p = true; last_p = pitch_rad;
  }
  if (!validf(last_r) || fabsf(roll_rad - last_r) >= DB_ATT_RAD) {
    changed_r = true; last_r = roll_rad;
  }

  if (!(changed_h || changed_p || changed_r)) return;                                                 // Exit if values have not changed

  StaticJsonDocument<512> doc;
  doc["context"] = "vessels.self";
  auto updates = doc.createNestedArray("updates");
  auto up      = updates.createNestedObject();
  up["$source"] = SK_SOURCE;
  auto values  = up.createNestedArray("values");

  auto add = [&](const char* path, float v) {
    auto o = values.createNestedObject();
    o["path"]  = path;
    o["value"] = v;
  };

  if (changed_h) add("navigation.headingMagnetic", last_h);     // SignalK paths and values
  if (changed_p) add("navigation.attitude.pitch",  last_p);
  if (changed_r) add("navigation.attitude.roll",   last_r);
  if (changed_h && send_hdg_true) {                             // Send navigation.headingTrue unless user has not switched this off
    float mv_rad = use_manual_magvar ? magvar_manual_rad : magvar_rad;
    auto wrap2pi = [](float r){ while (r < 0) r += 2.0f*M_PI; while (r >= 2.0f*M_PI) r -= 2.0f*M_PI; return r; };
    heading_true_rad = wrap2pi(last_h + mv_rad);
    heading_true_deg = heading_true_rad * RAD_TO_DEG;
    add("navigation.headingTrue", heading_true_rad);
  }

  if (values.size() == 0) return;

  char buf[640];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  bool ok = ws.send(buf, n);
  if (!ok) {
    ws.close();
    ws_open = false;
  }

  last_tx_ms = now;
}

// Send pitch and roll maximum values to SignalK if changed and less frequently than "live" values
void send_minmax_delta_if_due() {
  
  if (LCD_ONLY || !ws_open) return;                                                 // execute only if WiFi and Websocket ok

  static unsigned long last_minmax_tx_ms = 0;
  const unsigned long now = millis();
  if (now - last_minmax_tx_ms < MINMAX_TX_INTERVAL_MS) return;                      // execute only if timer is due

  static float last_sent_pitch_min = NAN, last_sent_pitch_max = NAN, last_sent_roll_min = NAN, last_sent_roll_max = NAN;

  bool ch_pmin = (validf(pitch_min_rad) && pitch_min_rad != last_sent_pitch_min);
  bool ch_pmax = (validf(pitch_max_rad) && pitch_max_rad != last_sent_pitch_max);
  bool ch_rmin = (validf(roll_min_rad)  && roll_min_rad  != last_sent_roll_min);
  bool ch_rmax = (validf(roll_max_rad)  && roll_max_rad  != last_sent_roll_max);

  if (!(ch_pmin || ch_pmax || ch_rmin || ch_rmax)) return;                          // execute only if values have been changed

  StaticJsonDocument<512> doc;
  doc["context"] = "vessels.self";
  auto updates = doc.createNestedArray("updates");
  auto up      = updates.createNestedObject();
  up["$source"] = SK_SOURCE;
  auto values  = up.createNestedArray("values");

  auto add = [&](const char* path, float v) {
    auto o = values.createNestedObject();
    o["path"]  = path;
    o["value"] = v; 
  };

  if (ch_pmin) add("navigation.attitude.pitch.min", pitch_min_rad);       // SignalK paths and values
  if (ch_pmax) add("navigation.attitude.pitch.max", pitch_max_rad);
  if (ch_rmin) add("navigation.attitude.roll.min",  roll_min_rad);
  if (ch_rmax) add("navigation.attitude.roll.max",  roll_max_rad);

  if (values.size() == 0) return;

  char buf[640];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  bool ok = ws.send(buf, n);
  if (!ok){
    ws.close();
    ws_open = false;
  }

  if (ch_pmin) last_sent_pitch_min = pitch_min_rad;
  if (ch_pmax) last_sent_pitch_max = pitch_max_rad;
  if (ch_rmin) last_sent_roll_min  = roll_min_rad;
  if (ch_rmax) last_sent_roll_max  = roll_max_rad;
  
  last_minmax_tx_ms = now;
}