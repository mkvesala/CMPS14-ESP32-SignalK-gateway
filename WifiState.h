#pragma once

#include <Arduino.h>

// === G L O B A L  W I F I S T A T E  E N U M  C L A S S ===
//
// - Global enum class WifiState for different states of WiFi connection
//   to be shared with whoever needs the state
// - Logic: the app (CMPS14Application) calls WiFi API (.isConnected() etc.)
//   and maintains the WifiState accordingly. This is to make other classes
//   independent from WiFi, keeping one source of truth by the app.

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