#pragma once

#include <Arduino.h>
#include <cmath>
#include <cstring>

namespace ESPNow {

    // Magic identifies our own packets among all ESP-NOW devices
    static constexpr uint32_t ESPNOW_MAGIC = 0x45534E57; // 'E''S''N''W'

    // Message types (extend as new sensors are added)
    enum class ESPNowMsgType : uint8_t {
        HEADING_DELTA   = 1,
        BATTERY_DELTA   = 2,
        WEATHER_DELTA   = 3,
        LEVEL_COMMAND   = 10,
        LEVEL_RESPONSE  = 11,
    };

    // === H E A D E R ===

    // Fixed 8-byte header for all messages
    // uint8_t payload_len: ESP-NOW max payload is 250 bytes, uint8_t is sufficient
    // reserved[2]: padding → header is 8 bytes, payload starts at a 4-byte boundary (floats aligned)
    struct ESPNowHeader {
        uint32_t magic;           // ESPNOW_MAGIC
        uint8_t  msg_type;        // ESPNowMsgType
        uint8_t  payload_len;     // payload length in bytes (max 250)
        uint8_t  reserved[2];     // padding, set to zero
    } __attribute__((packed));

    // === P A Y L O A D S ===

    // Compass / attitude
    // Sent by CMPS14-ESP32-SignalK-gateway
    struct HeadingDelta {
        float heading_rad;       // Magnetic heading (radians)
        float heading_true_rad;  // True heading (radians)
        float pitch_rad;         // Pitch (radians)
        float roll_rad;          // Roll (radians)
    };

    // Batteries
    // Sent by VEDirect-ESP32-SignalK-gateway
    struct BatteryDelta {
        float house_voltage;   // house bank volts
        float house_current;   // house bank amps, negative = charging
        float house_power;     // house bank watts
        float house_soc;       // house bank soc percent
        float start_voltage;   // starter battery volts
    };

    // Weather
    // Sent by BME280-ESP32-SignalK-gateway
    struct WeatherDelta {
        float temperature_c;   // °C
        float humidity_p;      // percent
        float pressure_hpa;    // hPa
    };

    // Level command (CrowPanel → Compass, broadcast)
    // Sent to CMPS14-ESP32-SignalK-gateway
    struct LevelCommand {
        uint8_t magic[4];     // "LVLC" — redundant in Phase 2 (msg_type identifies the packet)
        uint8_t reserved[4];
    };

    // Level response (Compass → CrowPanel, unicast)
    // Sent by CMPS14-ESP32-SignalK-gateway
    struct LevelResponse {
        uint8_t magic[4];     // "LVLR" — redundant in Phase 2 (msg_type identifies the packet)
        uint8_t success;      // 1 = OK, 0 = failed
        uint8_t reserved[3];
    };

    // === W R A P P E R ===

    template <typename TPayload>
    struct ESPNowPacket {
        ESPNowHeader hdr;
        TPayload payload;
    } __attribute__((packed));

    // === H E L P E R S ===

    inline void initHeader(ESPNowHeader& h, ESPNowMsgType type, uint8_t payload_len) {
        h.magic       = ESPNOW_MAGIC;
        h.msg_type    = static_cast<uint8_t>(type);
        h.payload_len = payload_len;
        h.reserved[0] = 0;
        h.reserved[1] = 0;
    }

    // === C R O W P A N E L  I N T E R N A L  D A T A ===

    // Internal struct for compass data, values stored as uint16_t/int16_t scaled x10.
    // Example: 234.5° stored as 2345.
    // Enables:
    //   - LVGL element rotation 0-3599 (lv_img_set_angle uses 0.1° units)
    //   - Integer arithmetic (fast, no FPU needed after conversion)
    //   - Easy human-readable formatting (2345 / 10 → 234°)

    struct HeadingData {
        uint16_t heading_mag_x10;   // Magnetic heading 0-3599 (0.0° - 359.9°)
        uint16_t heading_true_x10;  // True heading     0-3599 (0.0° - 359.9°)
        int16_t  pitch_x10;         // Pitch  -900 to  +900 (-90.0° to  +90.0°)
        int16_t  roll_x10;          // Roll  -1800 to +1800 (-180.0° to +180.0°)

        HeadingData()
            : heading_mag_x10(0)
            , heading_true_x10(0)
            , pitch_x10(0)
            , roll_x10(0)
        {}

        // Helpers for UI labels (full degrees)
        uint16_t getHeadingMagDeg()  const { return heading_mag_x10  / 10; }
        uint16_t getHeadingTrueDeg() const { return heading_true_x10 / 10; }
        int16_t  getPitchDeg()       const { return pitch_x10  / 10; }
        int16_t  getRollDeg()        const { return roll_x10   / 10; }
    };

    // Convert HeadingDelta (float radians, wire format) to HeadingData (int x10, internal)
    inline HeadingData convertDeltaToData(const HeadingDelta& delta) {
        HeadingData data;
        constexpr float RAD_TO_DEG_X10 = 180.0f * 10.0f / M_PI;

        // Heading: compass sends 0–2π, cast to uint16_t gives 0–3599
        float hdg_mag_x10  = delta.heading_rad      * RAD_TO_DEG_X10;
        float hdg_true_x10 = delta.heading_true_rad * RAD_TO_DEG_X10;
        data.heading_mag_x10  = (uint16_t)hdg_mag_x10;
        data.heading_true_x10 = (uint16_t)hdg_true_x10;

        // Pitch and roll: signed, keep sign
        data.pitch_x10 = (int16_t)(delta.pitch_rad * RAD_TO_DEG_X10);
        data.roll_x10  = (int16_t)(delta.roll_rad  * RAD_TO_DEG_X10);

        return data;
    }

} // namespace ESPNow
