#include "SignalKBroker.h"
#include "secrets.h"

using namespace websockets;

// === P U B L I C ===

// Constructor
SignalKBroker::SignalKBroker(CMPS14Processor &compassref)
    : compass(compassref) {
}

// Begin
bool SignalKBroker::begin() {
    if (strlen(SK_HOST)<= 0 || SK_PORT <= 0) return false;
    this->setSignalKURL();
    this->setSignalKSource();
    return this->connectWebsocket();
}

// Poll websocket - possibly other stuff if needed
void SignalKBroker::handleStatus() {

    // Keep websocket alive
    if (ws_open && ws) {
        ws->poll();
    }

    // This was a safety net to kill a ghost websocket
    // but the library should take care of it by
    // WebSocketsEvent::connectionClosed -event.
    // Removed as Wifi is now only on caller's (app) responsibility
    // if (!WiFi.isConnected() && ws_open) {
    //     ws.close();
    //     ws_open = false;
    // }
}

// Open Websocket to SignalK server and set callbacks.
// Build a brand-new client so every attempt starts from a clean TCP / lwIP socket state;
// a reused client can retain a stuck socket fd that never recovers until reboot.
bool SignalKBroker::connectWebsocket() {
    ws = std::make_unique<WebsocketsClient>();
    ws->onMessage([this](WebsocketsMessage msg) {
        this->onMessageCallback(msg);
    });
    ws->onEvent([this](WebsocketsEvent event, const String &data) {
        (void)data;                // const String &data not passed forward
        this->onEventCallback(event);
    });
    ws_open = ws->connect(SK_URL);
    if (ws_open) {
        last_pong_ms = millis();   // seed liveness so a fresh socket is not flagged stale
    } else {
        ws.reset();                // failed connect → destroy immediately, free the socket
    }
    return ws_open;
}

// Close websocket (web UI restart() handler and liveness graceful reconnect).
// Destroy the client so no stale transport state survives into the next reconnect.
void SignalKBroker::closeWebsocket() {
    if (ws) {
        ws->close();
        ws.reset();
    }
    ws_open = false;
    last_pong_ms = 0;
}

// Send a client-initiated ping frame to probe liveness
void SignalKBroker::ping() {
    if (ws_open && ws) ws->ping();
}

// Half-open detection: open, has been connected, but no pong within timeout
bool SignalKBroker::isStale(unsigned long now) const {
    return ws_open && last_pong_ms != 0 &&
           (long)(now - last_pong_ms) >= (long)PONG_TIMEOUT_MS;
}

// Send heading, pitch and roll to SignalK server
void SignalKBroker::sendHdgPitchRollDelta() {

    if (!ws_open) return;

    auto delta = compass.getHeadingDelta();
    if (!validf(delta.heading_rad) || !validf(delta.pitch_rad) || !validf(delta.roll_rad)) return;

    static float last_h = NAN;

    // Heading deadband gates the send; pitch and roll always included to match heading update rate
    if (validf(last_h) && fabsf(computeAngDiffRad(delta.heading_rad, last_h)) < DB_HDG_RAD) return;
    last_h = delta.heading_rad;

    hdg_pitch_roll_doc.clear();
    hdg_pitch_roll_doc["context"] = "vessels.self";
    auto updates = hdg_pitch_roll_doc.createNestedArray("updates");
    auto up = updates.createNestedObject();
    up["$source"] = SK_SOURCE;
    auto values = up.createNestedArray("values");

    auto add = [&](const char* path, float v) {
        auto o = values.createNestedObject();
        o["path"] = path;
        o["value"] = v;
    };

    add("navigation.headingMagnetic", last_h);
    add("navigation.attitude.pitch", delta.pitch_rad);
    add("navigation.attitude.roll", delta.roll_rad);
    if (compass.isSendingHeadingTrue()) add("navigation.headingTrue", delta.heading_true_rad);

    char buf[640];
    size_t n = serializeJson(hdg_pitch_roll_doc, buf, sizeof(buf));
    if (!ws->send(buf, n)) {
        this->closeWebsocket();   // destroy the client; backoff loop reconnects fresh
    }
}

// === P R I V A T E ===

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
  esp_efuse_mac_get_default(m); // get MAC address independently from WiFi library
  snprintf(SK_SOURCE, sizeof(SK_SOURCE), "esp32.cmps14-%02x%02x%02x", m[3], m[4], m[5]);
}

// Callback for onMessage, handle incoming SignalK delta 
void SignalKBroker::onMessageCallback(WebsocketsMessage msg) {
    if (!compass.isSendingHeadingTrue()) return;
    if (!msg.isText()) return;
    incoming_doc.clear();
    if (deserializeJson(incoming_doc, msg.data())) return;
    if (!incoming_doc.containsKey("updates")) return;
    for (JsonObject up : incoming_doc["updates"].as<JsonArray>()) {
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

// Callback for onEvent
void SignalKBroker::onEventCallback(WebsocketsEvent event) {
    switch (event) {
        case WebsocketsEvent::ConnectionOpened: {
            ws_open = true;
            last_pong_ms = millis();   // seed liveness on open
            this->handleVariationDelta();
            break;
        }
        case WebsocketsEvent::ConnectionClosed:
            ws_open = false;
            break;
        case WebsocketsEvent::GotPing:
            if (ws) ws->pong();
            break;
        case WebsocketsEvent::GotPong:
            last_pong_ms = millis();   // liveness refresh — feeds isStale()
            break;
        default:
            break;
    }
}

// When in heading true mode, subscribe the navigation.magneticVariation from SignalK at ~1 Hz cycles
void SignalKBroker::handleVariationDelta(){  
    if (!compass.isSendingHeadingTrue()) return;
    subscribe_doc.clear();
    subscribe_doc["context"] = "vessels.self";
    auto subscribe = subscribe_doc.createNestedArray("subscribe");
    auto s = subscribe.createNestedObject();
    s["path"] = "navigation.magneticVariation";
    s["format"] = "delta";
    s["policy"] = "ideal";
    s["period"] = 1000;

    char buf[256];
    size_t n = serializeJson(subscribe_doc, buf, sizeof(buf));
    if (ws) ws->send(buf, n);
}


