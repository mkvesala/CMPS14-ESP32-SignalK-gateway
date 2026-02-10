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
    void processLevelCommand();

private:
    
    CMPS14Processor &compass;

    bool initialized = false;

    // Attitude leveling command related static variables
    static uint8_t last_sender_mac[6];
    static volatile bool level_command_received;

    // Deadband tracking (same pattern as SignalKBroker)
    float last_h = NAN;
    float last_p = NAN;
    float last_r = NAN;

    static constexpr uint8_t BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static constexpr float DB_HDG_RAD = 0.00436f;  // 0.25° deadband for heading
    static constexpr float DB_ATT_RAD = 0.00436f;  // 0.25° deadband for pitch/roll

    // Static callback methods to be registered for ESP-NOW
    static void onDataSent(const esp_now_send_info_t* info, esp_now_send_status_t status);
    static void onDataRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len);
    
};
