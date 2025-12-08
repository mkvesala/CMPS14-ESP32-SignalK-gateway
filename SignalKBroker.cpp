#include "SignalKBroker.h"

// Constructor
SignalKBroker::SignalKBroker(CMPS14Processor &compassref)
    : compass(compassref) {
}

// Begin
bool SignalKBroker::begin() {
    if (strlen(SK_HOST)<= 0 || SK_PORT <= 0) return false;
    setSignalKURL();
    setSignalKSource();
    return connectWebsocket();
}

// Poll websocket and kill it if wifi has dropped but ws somehow still open
void SignalKBroker::handleStatus() {

    // Keep websocket alive
    if (ws_open) {
        ws.poll();
    }

    // Kill ghost websocket
    if (!WiFi.isConnected() && ws_open) {
        ws.close();
        ws_open = false;
    }
}

// Create SignalK server URL for websocket
void SignalKBroker::setSignalKURL() {
  if (strlen(SK_TOKEN) > 0)
    snprintf(SK_URL, sizeof(SK_URL), "ws://%s:%d/signalk/v1/stream?token=%s", SK_HOST, SK_PORT, SK_TOKEN);
  else
    snprintf(SK_URL, sizeof(SK_URL), "ws://%s:%d/signalk/v1/stream", SK_HOST, SK_PORT);
}

// Set ESP32's SignalK source based on ESP32's MAC address tail
void SignalKBroker::setSignalKSource() {
  uint8_t m[6];
  WiFi.macAddress(m);
  snprintf(SK_SOURCE, sizeof(SK_SOURCE), "esp32.cmps14-%02x%02x%02x", m[3], m[4], m[5]);
}

// Open Websocket to SignalK server and set callbacks
bool SignalKBroker::connectWebsocket() {
    ws_open = ws.connect(SK_URL);
    if (ws_open) {
        ws.onMessage([this](WebsocketsMessage msg) {
            this->onMessageCallback(msg);
        });
        ws.onEvent([this](WebsocketsEvent event, String data) {
            this->onEventCallback(event, data);
        });
    }
    return ws_open;
}

// Close websocket (used only from web UI restart() handler)
void SignalKBroker::closeWebsocket() {
    ws.close();
    ws_open = false;
}

// Callback for onEvent
void SignalKBroker::onEventCallback(WebsocketsEvent event, String data) {
    switch (event) {
        case WebsocketsEvent::ConnectionOpened:
            handleVariationDelta();
            break;
        case WebsocketsEvent::ConnectionClosed:
            ws_open = false;
            break;
        case WebsocketsEvent::GotPing:
            ws.pong();
            break;
        case WebsocketsEvent::GotPong:
        default:
            break;
    }
}

// Subscribe navigation.magneticVariation from SignalK server
void SignalKBroker::handleVariationDelta(){
    ws_open = true;
      
    // When in heading true mode, subscribe the navigation.magneticVariation from SignalK at ~1 Hz cycles
    if (!compass.isSendingHeadingTrue()) return;
    StaticJsonDocument<256> sub;
    sub["context"] = "vessels.self";
    auto subscribe = sub.createNestedArray("subscribe");
    auto s = subscribe.createNestedObject();
    s["path"] = "navigation.magneticVariation";
    s["format"] = "delta";
    s["policy"] = "ideal";
    s["period"] = 1000;

    char buf[256];
    size_t n = serializeJson(sub, buf, sizeof(buf));
    ws.send(buf, n);
}

// Callback for onMessage, handle incoming SignalK delta 
void SignalKBroker::onMessageCallback(WebsocketsMessage msg) {
    if (!compass.isSendingHeadingTrue()) return;
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
                    if (validf(mv)) { 
                        compass.setUseManualVariation(false);
                        compass.setLiveVariation(mv * RAD_TO_DEG);
                    } else compass.setUseManualVariation(true);
                }
            }
        }
    }
}

// Send heading, pitch and roll to SignalK server
void SignalKBroker::sendHdgPitchRollDelta() {
    auto delta = compass.getHeadingDelta();
  
    if (LCD_ONLY || !ws_open) return; 
    if (!validf(delta.heading_rad) || !validf(delta.pitch_rad) || !validf(delta.roll_rad)) return; 

    static float last_h = NAN, last_p = NAN, last_r = NAN;
    bool changed_h = false, changed_p = false, changed_r = false;

    if (!validf(last_h) || fabsf(ang_diff_rad(delta.heading_rad, last_h)) >= DB_HDG_RAD) {
        changed_h = true;
        last_h = delta.heading_rad;
    }
    if (!validf(last_p) || fabsf(delta.pitch_rad - last_p) >= DB_ATT_RAD) {
        changed_p = true;
        last_p = delta.pitch_rad;
    }
    if (!validf(last_r) || fabsf(delta.roll_rad - last_r) >= DB_ATT_RAD) {
        changed_r = true;
        last_r = delta.roll_rad;
    }

    if (!(changed_h || changed_p || changed_r)) return;  

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

    if (changed_h) add("navigation.headingMagnetic", last_h); 
    if (changed_p) add("navigation.attitude.pitch",  last_p);
    if (changed_r) add("navigation.attitude.roll",   last_r);
    if (changed_h && compass.isSendingHeadingTrue()) add("navigation.headingTrue", delta.heading_true_rad);

    if (values.size() == 0) return;

    char buf[640];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    bool ok = ws.send(buf, n);
    if (!ok) {
        ws.close();
        ws_open = false;
    }
}

// Send pitch and roll min/max values to SignalK
void SignalKBroker::sendPitchRollMinMaxDelta() {
    
    auto delta = compass.getMinMaxDelta();
  
    if (LCD_ONLY || !ws_open) return; 

    static float last_sent_pitch_min = NAN, last_sent_pitch_max = NAN, last_sent_roll_min = NAN, last_sent_roll_max = NAN;

    bool ch_pmin = (validf(delta.pitch_min_rad) && delta.pitch_min_rad != last_sent_pitch_min);
    bool ch_pmax = (validf(delta.pitch_max_rad) && delta.pitch_max_rad != last_sent_pitch_max);
    bool ch_rmin = (validf(delta.roll_min_rad)  && delta.roll_min_rad  != last_sent_roll_min);
    bool ch_rmax = (validf(delta.roll_max_rad)  && delta.roll_max_rad  != last_sent_roll_max);

    if (!(ch_pmin || ch_pmax || ch_rmin || ch_rmax)) return;

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

    if (ch_pmin) add("navigation.attitude.pitch.min", delta.pitch_min_rad); 
    if (ch_pmax) add("navigation.attitude.pitch.max", delta.pitch_max_rad);
    if (ch_rmin) add("navigation.attitude.roll.min",  delta.roll_min_rad);
    if (ch_rmax) add("navigation.attitude.roll.max",  delta.roll_max_rad);

    if (values.size() == 0) return;

    char buf[640];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    bool ok = ws.send(buf, n);
    if (!ok){
        ws.close();
        ws_open = false;
    }

    if (ch_pmin) last_sent_pitch_min = delta.pitch_min_rad;
    if (ch_pmax) last_sent_pitch_max = delta.pitch_max_rad;
    if (ch_rmin) last_sent_roll_min  = delta.roll_min_rad;
    if (ch_rmax) last_sent_roll_max  = delta.roll_max_rad;
}
