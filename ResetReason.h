#pragma once

#include <Arduino.h>
#include <esp_system.h>

// === G L O B A L  E N U M  C L A S S  R E S E T R E A S O N ===
//
// - Global enum class ResetReason for the firmware's own restart causes,
//   persisted to NVS just before ESP.restart() and read back on the next boot
//   - NONE: clean boot / power-on, or the stored reason was already consumed
//   - LOOP_WATCHDOG: loop runtime EMA stayed above LOOP_WATCHDOG_US
//   - NETWORK_WATCHDOG: WiFi associated but WebSocket silent for WS_WATCHDOG_MS
//   - WEBUI_RESTART: user pressed restart in the web UI
// - A crash (panic, brownout) never reaches the NVS write, so it is reported
//   separately via the ESP-IDF esp_reset_reason() mapping below
// - Both mappers return string literals: their addresses are stable for the
//   whole run, so they are safe to hand to ArduinoJson without copying

enum class ResetReason : uint8_t {
    NONE             = 0,
    LOOP_WATCHDOG    = 1,
    NETWORK_WATCHDOG = 2,
    WEBUI_RESTART    = 3
};

static inline const char* resetReasonToString(ResetReason reason) {
    switch (reason) {
        case ResetReason::LOOP_WATCHDOG:    return "LOOP WATCHDOG";
        case ResetReason::NETWORK_WATCHDOG: return "NETWORK WATCHDOG";
        case ResetReason::WEBUI_RESTART:    return "WEBUI RESTART";
        case ResetReason::NONE:             return "NONE";
        default:                            return "UNKNOWN";
    }
}

// ESP-IDF's own reset cause — covers the crashes the firmware cannot record itself
static inline const char* espResetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_SW:        return "SW_RESTART";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "OTHER_WDT";
        case ESP_RST_EXT:       return "EXT_RESET";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        default:                return "UNKNOWN";
    }
}
