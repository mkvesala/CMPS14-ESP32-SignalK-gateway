#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include "CMPS14Processor.h"

// === E S P N O W B R O K E R  C L A S S ===
//
// - Class ESPNowBroker - responsible for broadcasting compass data via ESP-NOW
// - Init: espnow.begin()
// - Provides public API to
//   - Initialize ESP-NOW in broadcast mode
//   - Send compass heading delta to all ESP-NOW listeners
// - Uses: CMPS14Processor ("the compass")
// - Phase 2+: Bidirectional communication with Crow Panel

class ESPNowBroker {
public:
    explicit ESPNowBroker(CMPS14Processor &compassref);

    bool begin();
    void sendHeadingDelta();

    // Phase 2+: Bidirectional communication
    // void setPeerAddress(const uint8_t* mac);
    // bool hasPeer() const { return _peer_set; }
    // void setCommandCallback(void (*cb)(uint8_t cmd, int16_t p1, int16_t p2));

private:
    CMPS14Processor &compass;

    bool _initialized = false;

    // Deadband tracking (same pattern as SignalKBroker)
    float _last_h = NAN;
    float _last_p = NAN;
    float _last_r = NAN;

    static constexpr uint8_t BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static constexpr float DB_HDG_RAD = 0.00436f;  // 0.25° deadband for heading
    static constexpr float DB_ATT_RAD = 0.00436f;  // 0.25° deadband for pitch/roll

    static void onDataSent(const uint8_t* mac, esp_now_send_status_t status);

    // Phase 2+:
    // static void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);
};
