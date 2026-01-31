#include "ESPNowBroker.h"
#include <WiFi.h>

// Static member initialization
constexpr uint8_t ESPNowBroker::BROADCAST_ADDR[6];

// Constructor
ESPNowBroker::ESPNowBroker(CMPS14Processor &compassref)
    : compass(compassref) {
}

// Initialize ESP-NOW
bool ESPNowBroker::begin() {
    if (esp_now_init() != ESP_OK) {
        // Serial.println("ESP-NOW init failed");
        return false;
    }

    // Register broadcast peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
    peer.channel = 0;  // 0 = use current WiFi channel
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("ESP-NOW add peer failed");
        return false;
    }

    esp_now_register_send_cb(onDataSent);

    _initialized = true;
    Serial.println("ESP-NOW bridge initialized (broadcast mode)");
    return true;
}

void ESPNowBroker::sendHeadingDelta() {
    if (!_initialized) return;

    auto delta = compass.getHeadingDelta();

    // Validate data
    if (!validf(delta.heading_rad) || !validf(delta.pitch_rad) || !validf(delta.roll_rad)) return;

    // Deadband check (same logic as SignalKBroker::sendHdgPitchRollDelta)
    bool changed_h = false, changed_p = false, changed_r = false;

    if (!validf(_last_h) || fabsf(computeAngDiffRad(delta.heading_rad, _last_h)) >= DB_HDG_RAD) {
        changed_h = true;
        _last_h = delta.heading_rad;
    }
    if (!validf(_last_p) || fabsf(delta.pitch_rad - _last_p) >= DB_ATT_RAD) {
        changed_p = true;
        _last_p = delta.pitch_rad;
    }
    if (!validf(_last_r) || fabsf(delta.roll_rad - _last_r) >= DB_ATT_RAD) {
        changed_r = true;
        _last_r = delta.roll_rad;
    }

    // Only send if something changed
    if (!(changed_h || changed_p || changed_r)) return;

    // Send delta directly
    esp_now_send(BROADCAST_ADDR, (const uint8_t*)&delta, sizeof(delta));
}

void ESPNowBroker::onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
    // Phase 1: No action needed
    // Phase 2+: Diagnostics/retry logic if needed
}
