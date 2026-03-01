#include "espnow_protocol.h"
#include "ESPNowBroker.h"
#include <WiFi.h>

// === S T A T I C ===

uint8_t ESPNowBroker::last_sender_mac[6] = {0};
volatile bool ESPNowBroker::level_command_received = false;

// === P U B L I C ===

// Constructor
ESPNowBroker::ESPNowBroker(CMPS14Processor &compassref) : compass(compassref) {}

// Initialize ESP-NOW
bool ESPNowBroker::begin() {

    if (esp_now_init() != ESP_OK) return false;

    // Register broadcast peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
    peer.channel = 0; // Current WiFi channel
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) != ESP_OK) return false;

    // Register callbacks
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    initialized = true;

    return true;
}

// Send heading delta from the compass as ESP-NOW broadcast packet
void ESPNowBroker::sendHeadingDelta() {
    
    if (!initialized) return;

    auto delta = compass.getHeadingDelta();

    // Validate data
    if (!validf(delta.heading_rad) || !validf(delta.pitch_rad) || !validf(delta.roll_rad)) return;

    // Deadband check (same logic as SignalKBroker::sendHdgPitchRollDelta())
    bool changed_h = false, changed_p = false, changed_r = false;

    if (!validf(last_h) || fabsf(computeAngDiffRad(delta.heading_rad, last_h)) >= DB_HDG_RAD) {
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

    // Only send if something changed
    if (!(changed_h || changed_p || changed_r)) return;

    // Send delta as ESP-NOW packet payload
    ESPNow::ESPNowPacket<ESPNow::HeadingDelta> pkt;
    initHeader(pkt.hdr, ESPNow::ESPNowMsgType::HEADING_DELTA, sizeof(ESPNow::HeadingDelta));
    memcpy(&pkt.payload, &delta, sizeof(ESPNow::HeadingDelta));
    esp_now_send(BROADCAST_ADDR, (const uint8_t*)&pkt, sizeof(pkt));
}

// Process the received attitude leveling command coming from ESP-NOW peer
void ESPNowBroker::processLevelCommand() {
    if(!level_command_received) return;
    level_command_received = false;

    // Run the actual leveling
    compass.level();

    // Create the response packet
    ESPNow::ESPNowPacket<ESPNow::LevelResponse> pkt;
    // Header
    initHeader(pkt.hdr, ESPNow::ESPNowMsgType::LEVEL_RESPONSE, sizeof(ESPNow::LevelResponse));
    // Payload
    memcpy(pkt.payload.magic, "LVLR", 4);
    pkt.payload.success = 1;
    memset(pkt.payload.reserved, 0, 3);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, last_sender_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;

    if (!esp_now_is_peer_exist(last_sender_mac)) esp_now_add_peer(&peer);
    // Send packet
    esp_err_t result = esp_now_send(last_sender_mac, (const uint8_t*)&pkt, sizeof(pkt));
}

// === P R I V A T E ===

// Static callback for data send
void ESPNowBroker::onDataSent(const esp_now_send_info_t* info, esp_now_send_status_t status) {}

// Static callback for data receive
void ESPNowBroker::onDataRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
    if (len < (int)sizeof(ESPNow::ESPNowHeader)) return;
    ESPNow::ESPNowHeader hdr;
    memcpy(&hdr, data, sizeof(ESPNow::ESPNowHeader));
    if (hdr.magic != ESPNow::ESPNOW_MAGIC) return;
    if (len < (int)(sizeof(ESPNow::ESPNowHeader) + hdr.payload_len)) return;
    if (static_cast<ESPNow::ESPNowMsgType>(hdr.msg_type) == ESPNow::ESPNowMsgType::LEVEL_COMMAND) {
        if (hdr.payload_len != sizeof(ESPNow::LevelCommand)) return;
        memcpy(last_sender_mac, recv_info->src_addr, 6);
        level_command_received = true;
    }
}
