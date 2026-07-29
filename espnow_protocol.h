#pragma once

#include <Arduino.h>
#include <cmath>
#include <cstring>

// === S H A R E D  E S P - N O W  P R O T O C O L ===
//
// This file is copied by hand into every ESP32 project on the boat. All devices must
// agree on it, but no single project owns it. On 2026-07-28 the copies had drifted into
// four versions; they were merged into this superset and every project now holds it.
//
// Keeping it that way: write a change in whichever project needs it, then copy the whole
// file to every other project — the change is not done until all copies are identical.
// Never reconcile two versions field by field; that is how the four versions came about.
//
// ESPNowMsgType is a single fleet-wide number space. Before allocating a new value,
// check every copy, not just the sender's and the receiver's:
//     grep -A16 'enum class ESPNowMsgType' ~/Documents/Arduino/*/espnow_protocol.h
//
// Senders:
//   1 HEADING_DELTA        CMPS14-ESP32-SignalK-gateway
//   2 BATTERY_DELTA        VEDirect-ESP32-SignalK-gateway
//   3 WEATHER_DELTA        BME280-ESP32-SignalK-gateway
//   4 GNSS_DELTA           UBLOX-ESP32-SignalK-gateway
//   5 HALMET_ENGINE_DELTA  HALMET-ESP32-SignalK-gateway
//   6 HALMET_TANK_DELTA    HALMET-ESP32-SignalK-gateway
//   7 HALMET_WATER_DELTA   HALMET-ESP32-SignalK-gateway
//   8 DEPTH_DELTA          SignalK-ESP-NOW-gateway
//
// Receivers: ESP32-Crowpanel-SkippersWatch, ESP32-Crowpanel-compass, DFWind-ESP32-SignalK-gateway

namespace ESPNow {

    // Magic identifies our own packets among all ESP-NOW devices
    static constexpr uint32_t ESPNOW_MAGIC = 0x45534E57; // 'E''S''N''W'

    // Message types (extend as new sensors are added)
    enum class ESPNowMsgType : uint8_t {
        HEADING_DELTA        = 1,
        BATTERY_DELTA        = 2,
        WEATHER_DELTA        = 3,
        GNSS_DELTA           = 4,
        HALMET_ENGINE_DELTA  = 5,
        HALMET_TANK_DELTA    = 6,
        HALMET_WATER_DELTA   = 7,
        DEPTH_DELTA          = 8,
        // Reserved for runtime-configurable path relaying — NOT implemented.
        // A self-describing message would let a web UI add SignalK paths without a
        // firmware change at either end, at the cost of 48 bytes of path per packet:
        //   struct GenericSKDelta { char path[48]; float value; uint32_t age_ms; };
        // GENERIC_SK_DELTA  = 20,
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

    // GNSS position, speed, course
    // Sent by UBLOX-ESP32-SignalK-gateway
    struct GnssDelta {
        float lat_deg;        // Latitude, decimal degrees
        float lon_deg;        // Longitude, decimal degrees
        float sog_ms;         // Speed over ground, m/s
        float cog_true_rad;   // Course over ground (true), radians
        float mag_var_rad;    // Magnetic variation (WMM), radians — NAN until first fix
        uint8_t satellites;   // SIV
        uint8_t fix_type;     // 0=no fix, 3=3D, 4=GNSS+DR
        uint8_t fix_ok;       // getGnssFixOk() ? 1 : 0
        uint8_t reserved;     // padding
    };  // 24 bytes

    // Engine data
    // Sent by HALMET-ESP32-SignalK-gateway
    struct HALMETEngineDelta {
        float exhaust_temp_k;    // propulsion.0.exhaustTemperature [K]
    };

    // Tank data
    // Sent by HALMET-ESP32-SignalK-gateway
    struct HALMETTankDelta {
        float fuel_level_ratio;  // tanks.fuel.0.currentLevel [0.0..1.0]
    };

    // Fresh water tank data
    // Sent by HALMET-ESP32-SignalK-gateway
    struct HALMETWaterDelta {
        float water_level_ratio;  // tanks.freshWater.0.currentLevel [0.0..1.0]
    };

    // Depth relayed from the SignalK server, not measured locally.
    // Source chain: Raymarine Element 12S sounder → NMEA2000 → SH-wg → UDP → SignalK.
    // Sent by SignalK-ESP-NOW-gateway.
    //
    // A relayed value carries no freshness of its own, so the receiver cannot tell a live
    // reading from a frozen one. Both signals below are therefore required:
    //   - a field is NAN when that individual path is stale or has never arrived
    //   - age_ms describes the depth feed as a whole (bottom lost, or N2K chain down)
    struct DepthDelta {
        float    below_surface_m;     // environment.depth.belowSurface [m]
        float    below_transducer_m;  // environment.depth.belowTransducer [m]
        float    below_keel_m;        // environment.depth.belowKeel [m]
        uint32_t age_ms;              // ms since the freshest of the three; UINT32_MAX = never received
    };  // 16 bytes

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
    //
    // Receiver-side only: the CrowPanel displays convert incoming wire structs into these
    // once, then work in integers. Not part of the wire format — sending projects compile
    // them but never use them (structs and inline functions, so nothing is emitted).

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

    // Internal struct for GNSS data, values stored scaled x10 for integer arithmetic.
    //
    // fix_ok and cog_valid are DISTINCT and must stay that way. A stationary boat (at
    // anchor) has a valid position fix but an undefined course: fix_ok == 1, cog_valid == 0.
    // Consumers gate position/SOG on hasFix() and COG on hasCog() — never fold the two
    // together (an earlier version did, which hid position + SOG at anchor).
    struct GnssData {
        uint16_t cog_true_x10;   // COG true 0-3599 (0.0° - 359.9°); meaningful only if cog_valid
        uint16_t sog_knots_x10;  // SOG in knots × 10 (e.g. 72 = 7.2 kn); 0 when no fix
        uint8_t  fix_ok;         // 1 = valid POSITION fix (getGnssFixOk()) — independent of COG
        bool     cog_valid;      // 1 = COG present (moving); 0 when stationary (COG was NaN)
        float    lat_deg;        // Latitude, decimal degrees (watch POS display; NAN until fix)
        float    lon_deg;        // Longitude, decimal degrees (watch POS display; NAN until fix)

        GnssData()
            : cog_true_x10(0)
            , sog_knots_x10(0)
            , fix_ok(0)
            , cog_valid(false)
            , lat_deg(NAN)
            , lon_deg(NAN)
        {}

        uint16_t getCogDeg()   const { return cog_true_x10  / 10; }
        float    getSogKnots() const { return sog_knots_x10 / 10.0f; }
        bool     hasFix()      const { return fix_ok == 1; }
        bool     hasCog()      const { return cog_valid; }
    };

    // Convert GnssDelta (float wire format) to GnssData (int x10, internal)
    inline GnssData convertGnssDeltaToData(const GnssDelta& delta) {
        GnssData data;
        constexpr float RAD_TO_DEG_X10  = 180.0f * 10.0f / M_PI;
        constexpr float MS_TO_KNOTS_X10 = 1.94384f * 10.0f;

        // Position fix validity is authoritative and must NOT be diluted by COG availability.
        // A stationary boat still has a valid fix, position and SOG — only its course is
        // undefined. (Folding COG-NaN into fix_ok hid position/SOG at anchor, exactly the
        // anchored-glance case the watch exists for.)
        data.fix_ok = delta.fix_ok;

        // COG is NaN when stationary (UB cast → 0xFFFF garbage) — tracked separately from fix.
        data.cog_valid     = delta.fix_ok && !std::isnan(delta.cog_true_rad);
        data.cog_true_x10  = data.cog_valid ? (uint16_t)(delta.cog_true_rad * RAD_TO_DEG_X10) : 0;
        data.sog_knots_x10 = delta.fix_ok ? (uint16_t)(delta.sog_ms * MS_TO_KNOTS_X10) : 0;

        // Position for the watch POS label — valid only with a fix (same flag as fix_ok now)
        data.lat_deg = delta.fix_ok ? delta.lat_deg : NAN;
        data.lon_deg = delta.fix_ok ? delta.lon_deg : NAN;

        return data;
    }

} // namespace ESPNow
