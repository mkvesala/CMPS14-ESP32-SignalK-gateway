#pragma once

#include <Arduino.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <esp_mac.h>
#include <memory>
#include "CMPS14Processor.h"

// === S I G N A L K B R O K E R  C L A S S ===
//
// - Class SignalKBroker - "the signalk" responsible for 
//   communicating with SignalK server over websocket
// - Init: signalk.begin()
// - Provides public API to
//   - Connect and disconnect the websocket
//   - Send SignalK deltas as JSON to the server
//   - Get the source name that is visible to the server
//   - Check the websocket connection status
// - Uses: CMPS14Processor ("the compass")
// - Owns: WebsocketsClient

namespace websockets {
    class WebsocketsClient;
    class WebsocketsMessage;
    enum class WebsocketsEvent;
}

class SignalKBroker {
public:
    explicit SignalKBroker(CMPS14Processor &compassref);

    bool begin();
    void handleStatus();
    bool connectWebsocket();
    void closeWebsocket();
    void sendHdgPitchRollDelta();
    const char* getSignalKSource() { return SK_SOURCE; }
    bool isOpen() const { return ws_open; }
    void ping();                             // send a client ping frame if open
    bool isStale(unsigned long now) const;   // open but no pong within PONG_TIMEOUT_MS

private:

    void setSignalKURL();
    void setSignalKSource();
    void onMessageCallback(websockets::WebsocketsMessage msg);
    void onEventCallback(websockets::WebsocketsEvent event);
    void handleVariationDelta();

private:
    
    CMPS14Processor &compass;
    std::unique_ptr<websockets::WebsocketsClient> ws;  // fresh instance every connect

    // Reusable JSON documents
    StaticJsonDocument<512> hdg_pitch_roll_doc;
    StaticJsonDocument<1024> incoming_doc;
    StaticJsonDocument<256> subscribe_doc;

    bool ws_open = false;

    // Liveness — half-open TCP detection via client ping / server pong
    static constexpr unsigned long PONG_TIMEOUT_MS = 29989UL;  // ~30 s w/o pong → stale
    unsigned long last_pong_ms = 0;   // millis() of last GotPong / open; 0 = not connected

    char SK_URL[512];     // URL of SignalK server
    char SK_SOURCE[32];   // ESP32 source name for SignalK, used also as the OTA hostname
    static constexpr float DB_HDG_RAD = 0.0008725f;    // 0.05°: deadband threshold for heading
};
