#pragma once

#include <stdint.h>

// Domain type for calibration mode
enum CalMode : uint8_t {
    CAL_USE       = 0,
    CAL_FULL_AUTO = 1,
    CAL_SEMI_AUTO = 2,
    CAL_MANUAL    = 3
};

// Domain helper for converting mode to human-readable string
static inline const char* calModeToString(CalMode mode) {
    switch (mode) {
        case CAL_FULL_AUTO: return "FULL AUTO";
        case CAL_SEMI_AUTO: return "AUTO";
        case CAL_MANUAL:    return "MANUAL";
        case CAL_USE:       return "USE";
        default:            return "UNKNOWN";
    }
}
