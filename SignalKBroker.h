#pragma once

#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include "globals.h"
#include "CMPS14Processor.h"

using namespace websockets;

class SignalKBroker {
public:
    explicit SignalKBroker(CMPS14Processor &compassref);

    bool begin();
    void handleStatus();
    bool connectWebsocket();
    void closeWebsocket();
    void sendHdgPitchRollDelta();
    void sendPitchRollMinMaxDelta();
    const char* getSignalKSource() { return SK_SOURCE; }
    bool isOpen() const { return ws_open; }

private:

    float computeAngDiffRad(float a, float b);
    void setSignalKURL();
    void setSignalKSource();
    void onMessageCallback(WebsocketsMessage msg);
    void onEventCallback(WebsocketsEvent event, String data);
    void handleVariationDelta();

private:
    
    CMPS14Processor &compass;
    WebsocketsClient ws;

    bool ws_open = false;

    char SK_URL[512];     // URL of SignalK server
    char SK_SOURCE[32];   // ESP32 source name for SignalK, used also as the OTA hostname
    static constexpr float DB_HDG_RAD = 0.00436f;    // 0.25°: deadband threshold for heading
    static constexpr float DB_ATT_RAD = 0.00436f;    // 0.25°: pitch/roll deadband threshold
};
