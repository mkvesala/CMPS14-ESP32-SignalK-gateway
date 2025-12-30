#pragma once

#include <Arduino.h>

// === G L O B A L  E N U M  C L A S S  C A L M O D E ===
//
// - Global enum class CalMode for 4 different calibration modes of CMPS14
//   to be shared with anyone who needs that
//   - USE: normal operation, no calibration running
//   - FULL_AUTO: CMPS14 built in autocalibration / autosave
//   - AUTO: start automatically at boot and save when enough state reached
//   - MANUAL: user initiated calibration, user must save

enum class CalMode : uint8_t {
    USE       = 0,
    FULL_AUTO = 1,
    AUTO      = 2,
    MANUAL    = 3
};

static inline const char* calModeToString(CalMode mode) {
    switch (mode) {
        case CalMode::FULL_AUTO:     return "FULL AUTO";
        case CalMode::AUTO:          return "AUTO";
        case CalMode::MANUAL:        return "MANUAL";
        case CalMode::USE:           return "USE";
        default:                     return "UNKNOWN";
    }
}
