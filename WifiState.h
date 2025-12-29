#pragma once

#include <Arduino.h>

enum class WifiState : uint8_t {
    INIT            = 0,
    CONNECTING      = 1,
    CONNECTED       = 2,
    FAILED          = 3,
    DISCONNECTED    = 4,
    OFF             = 5
};

// Domain helper for converting wifi state to human-readable string
static inline const char* wifiStateToString(WifiState state) {
    switch (state) {
        case WifiState::INIT:         return "INIT";
        case WifiState::CONNECTING:   return "CONNECTING";
        case WifiState::CONNECTED:    return "CONNECTED";
        case WifiState::FAILED:       return "FAILED";
        case WifiState::DISCONNECTED: return "DISCONNECTED";
        case WifiState::OFF:          return "OFF";
        default:                      return "UNKNOWN";
    }
}