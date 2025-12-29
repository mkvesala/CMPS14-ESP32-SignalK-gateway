#pragma once

#include <Arduino.h>

// Domain type for calibration mode
enum class CalMode : uint8_t {
    USE       = 0,
    FULL_AUTO = 1,
    AUTO      = 2,
    MANUAL    = 3
};

// Domain helper for converting mode to human-readable string
static inline const char* calModeToString(CalMode mode) {
    switch (mode) {
        case CalMode::FULL_AUTO:     return "FULL AUTO";
        case CalMode::AUTO:          return "AUTO";
        case CalMode::MANUAL:        return "MANUAL";
        case CalMode::USE:           return "USE";
        default:                     return "UNKNOWN";
    }
}
