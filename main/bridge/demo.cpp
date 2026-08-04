#include "bridge/demo.hpp"

#include <atomic>
#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "demo";

namespace bridge {

// ---------------------------------------------------------------------------
// MAVLink v2 framing: CRC + encode + decode.
// ---------------------------------------------------------------------------

// MAVLink v2 CRC (CRC-16/MCRF4XX, the X.25 variant MAVLink uses). This is
// the wire-level checksum Galapagos verifies on every frame; getting it
// wrong means "no vehicle identified" even though bytes arrive.
static uint16_t crc16_mcrf4xx(const uint8_t* data, size_t len, uint16_t seed) {
    uint16_t crc = seed;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0x8408;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// CRC_EXTRA per message id — the per-dialect constant appended before the
// checksum. These are the exact values the mavlink crate emits (verified
// against its generated ardupilotmega dialect); a wrong value makes
// Galapagos reject every frame from this bridge.
static uint8_t crc_extra(uint32_t id) {
    switch (id) {
        case 0:   return 50;  // HEARTBEAT
        case 1:   return 124; // SYS_STATUS
        case 20:  return 214; // PARAM_REQUEST_READ
        case 21:  return 159; // PARAM_REQUEST_LIST
        case 22:  return 220; // PARAM_VALUE
        case 23:  return 168; // PARAM_SET
        case 24:  return 24;  // GPS_RAW_INT
        case 30:  return 39;  // ATTITUDE
        case 33:  return 104; // GLOBAL_POSITION_INT
        case 42:  return 28;  // MISSION_CURRENT
        case 43:  return 132; // MISSION_REQUEST_LIST
        case 44:  return 221; // MISSION_COUNT
        case 45:  return 232; // MISSION_CLEAR_ALL
        case 47:  return 153; // MISSION_ACK
        case 51:  return 196; // MISSION_REQUEST_INT
        case 65:  return 118; // RC_CHANNELS
        case 70:  return 124; // RC_CHANNELS_OVERRIDE
        case 73:  return 38;  // MISSION_ITEM_INT
        case 74:  return 20;  // VFR_HUD
        case 76:  return 152; // COMMAND_LONG
        case 77:  return 143; // COMMAND_ACK
        case 109: return 185; // RADIO_STATUS
        case 117: return 128; // LOG_REQUEST_LIST
        case 118: return 56;  // LOG_ENTRY
        case 119: return 116; // LOG_REQUEST_DATA
        case 120: return 134; // LOG_DATA
        case 122: return 203; // LOG_REQUEST_END
        case 147: return 154; // BATTERY_STATUS
        case 253: return 83;  // STATUSTEXT
        default:  return 0;
    }
}

// Little-endian readers/writers over a byte buffer.
static uint16_t get_u16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
static uint32_t get_u32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
static int32_t get_i32(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }
static float get_f32(const uint8_t* p) { float v; std::memcpy(&v, p, 4); return v; }
static void put_f32(uint8_t* out, size_t off, float v) { std::memcpy(out + off, &v, 4); }
static void put_u32(uint8_t* out, size_t off, uint32_t v) { std::memcpy(out + off, &v, 4); }
static void put_i32(uint8_t* out, size_t off, int32_t v) { std::memcpy(out + off, &v, 4); }
static void put_u16(uint8_t* out, size_t off, uint16_t v) { std::memcpy(out + off, &v, 2); }
static void put_i16(uint8_t* out, size_t off, int16_t v) { std::memcpy(out + off, &v, 2); }

// IP of the GCS, learned from WIFI_EVENT_AP_STAIPASSIGNED. 0 = unknown.
// Guarded by a spinlock-ish atomic because the WiFi event handler (another
// task) writes it while demo_task reads it every frame.
static std::atomic<uint32_t> s_peer_ip{0};

void demo_set_peer(uint32_t ipv4) {
    s_peer_ip.store(ipv4, std::memory_order_release);
    ESP_LOGI(TAG, "GCS peer IP: %u.%u.%u.%u",
             (ipv4 >> 24) & 0xFF, (ipv4 >> 16) & 0xFF,
             (ipv4 >> 8) & 0xFF, ipv4 & 0xFF);
}

void demo_send_frame(UdpRelay& relay, uint32_t msgid, std::span<const uint8_t> payload) {
    // MAVLink v2 header: STX(0xFD) len incompat compat seq sysid compid msgid(3 LE)
    uint8_t frame[10 + 255 + 2];
    frame[0] = 0xFD;
    frame[1] = static_cast<uint8_t>(payload.size());
    frame[2] = 0;
    frame[3] = 0;
    frame[4] = 0;           // sequence — incremented by caller if it cares
    frame[5] = 1;           // system_id
    frame[6] = 1;           // component_id
    frame[7] = static_cast<uint8_t>(msgid & 0xFF);
    frame[8] = static_cast<uint8_t>((msgid >> 8) & 0xFF);
    frame[9] = static_cast<uint8_t>((msgid >> 16) & 0xFF);
    std::memcpy(frame + 10, payload.data(), payload.size());

    // MAVLink checksum covers the post-STX header (len, seq, sys, comp,
    // msgid) + payload + the crc_extra byte fed through the same CRC chain
    // (not XOR'd in). Verified byte-for-byte against the mavlink crate's
    // encoding: crc16(frame[1..10] + payload + [extra]).
    uint16_t crc = crc16_mcrf4xx(frame + 1, 9 + payload.size(), 0xFFFF);
    const uint8_t extra = crc_extra(msgid);
    crc = crc16_mcrf4xx(&extra, 1, crc);
    frame[10 + payload.size()] = static_cast<uint8_t>(crc & 0xFF);
    frame[10 + payload.size() + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    // Always broadcast (matches the SITL/desktop path). Once a GCS peer has
    // been learned from WIFI_EVENT_AP_STAIPASSIGNED, also unicast to it —
    // iOS is far more reliable at *unicast* than broadcast on a local net.
    auto frame_span = std::span<const uint8_t>(frame, 12 + payload.size());
    relay.send_broadcast(frame_span);
    uint32_t peer = s_peer_ip.load(std::memory_order_acquire);
    if (peer != 0) {
        relay.send_unicast_to(peer, 14550, frame_span);
    }
}

// Decode a complete MAVLink v2 frame from `data` (full datagram). Returns
// the message id, or 0xFFFFFFFF if the frame is invalid (bad magic, bad
// length, or checksum mismatch). `payload_out`/`len_out` point into `data`.
static uint32_t demo_decode_frame(const uint8_t* data, size_t n,
                                  const uint8_t** payload_out, size_t* len_out) {
    if (n < 10 || data[0] != 0xFD) return 0xFFFFFFFF;
    size_t plen = data[1];
    if (n < 12 + plen) return 0xFFFFFFFF;
    uint32_t msgid = data[7] | (data[8] << 8) | (data[9] << 16);

    uint16_t crc = crc16_mcrf4xx(data + 1, 9 + plen, 0xFFFF);
    const uint8_t extra = crc_extra(msgid);
    crc = crc16_mcrf4xx(&extra, 1, crc);
    uint16_t wire = data[10 + plen] | (data[11 + plen] << 8);
    if (crc != wire) {
        ESP_LOGW(TAG, "frame msgid=%lu CRC mismatch (got %04x want %04x)",
                 (unsigned long)msgid, wire, crc);
        return 0xFFFFFFFF;
    }
    *payload_out = data + 10;
    *len_out = plen;
    return msgid;
}

// ---------------------------------------------------------------------------
// Synthetic vehicle state
// ---------------------------------------------------------------------------

static bool s_armed = false;
static uint8_t s_mode = 0;   // ArduPilot Copter custom_mode (0=Stabilize)
static float s_t = 0.0f;
static float s_lat = 47.397742f;
static float s_lon = 8.545594f;
static float s_alt = 42.0f;
static float s_heading = 0.0f;
static float s_batt_v = 16.8f;
static uint8_t s_batt_pct = 92;
static uint8_t s_rssi = 210;

static const char* const kModeNames[] = {
    "Stabilize", "Acro", "AltHold", "Auto", "Guided",
    "Loiter", "RTL", "Circle", "", "Land",
};

struct ParamDef {
    const char* id;
    float value;
    uint8_t type; // MAV_PARAM_TYPE_*
};
// A small but real-looking ArduPilot parameter set — mixed types, the same
// mix a real vehicle reports. Param indices are 0-based wire order.
static ParamDef s_params[] = {
    {"FENCE_ENABLE", 1.0f, 1},   // INT8
    {"FENCE_ALT_MAX", 120.0f, 9},// REAL32
    {"BATT_CAPACITY", 5200.0f, 6},// INT32
    {"RTL_ALT", 30.0f, 9},       // REAL32
    {"WPNAV_SPEED", 800.0f, 6},  // INT32
    {"SYSID_THISMAV", 1.0f, 4},  // INT16
    {"BATT_LOW_VOLT", 14.4f, 9}, // REAL32
    {"THR_MIN", 130.0f, 4},      // INT16
    {"FRAME_CLASS", 1.0f, 1},    // INT8
    {"ARMING_CHECK", 1.0f, 1},   // INT8
    {"COMPASS_OFFS_X", 0.0f, 9}, // REAL32
    {"AHRS_TRIM_X", 0.0f, 9},    // REAL32
    {"ATC_ACCEL_MAX", 9.0f, 9},  // REAL32
    {"PILOT_SPEED_UP", 250.0f, 4},// INT16
};
static constexpr size_t kParamCount = sizeof(s_params) / sizeof(s_params[0]);

static constexpr uint8_t kMaxMission = 32;
struct MissionItemStorage {
    uint8_t seq;
    uint16_t cmd;
    float p1, p2, p3, p4;
    int32_t x, y;
    float z;
};
static MissionItemStorage s_mission[kMaxMission];
static uint16_t s_mission_count = 0;
static uint16_t s_mission_pending = 0; // items awaited during upload handshake

// A few fake onboard logs for the log browser + download buttons.
static const struct { uint16_t id; uint32_t size; uint32_t t; } s_logs[] = {
    {0, 8192, 1700000000u},
    {1, 16384, 1700100000u},
    {2, 4096, 1700200000u},
};
static constexpr size_t kLogCount = sizeof(s_logs) / sizeof(s_logs[0]);

// ---------------------------------------------------------------------------
// Outbound helpers
// ---------------------------------------------------------------------------

static void send_heartbeat(UdpRelay& relay) {
    uint8_t p[9] = {0};
    put_u32(p, 0, s_mode);
    p[4] = 2;  // MAV_TYPE_QUADROTOR
    p[5] = 3;  // MAV_AUTOPILOT_ARDUPILOTMEGA
    p[6] = 1 | (s_armed ? 128 : 0); // CUSTOM_MODE_ENABLED | SAFETY_ARMED
    p[7] = 4;  // MAV_STATE_ACTIVE
    p[8] = 3;
    demo_send_frame(relay, 0, p);
}

static void send_sys_status(UdpRelay& relay) {
    // SYS_STATUS (msgid 1), payload 40 bytes: present(4) enabled(4) healthy(4)
    // load(2) voltage(2) current(2) drop(2) errors_comm(2) errors1..4(2x4)
    // battery_remaining(1) + extensions.
    uint8_t p[40] = {0};
    uint32_t sensors = 0x2F802102;
    put_u32(p, 0, sensors);
    put_u32(p, 4, sensors);
    put_u32(p, 8, sensors);
    put_u16(p, 12, 55);   // load %
    put_u16(p, 14, static_cast<uint16_t>(s_batt_v * 1000.0f));
    put_u16(p, 16, 8400); // current cA (negative = discharging)
    put_u16(p, 18, 0);    // drop_rate_comm
    put_u16(p, 20, 0);    // errors_comm
    p[28] = s_batt_pct;   // battery_remaining (byte 28 in 40-byte layout)
    demo_send_frame(relay, 1, std::span<const uint8_t>(p, sizeof p));
}

static void send_gps(UdpRelay& relay) {
    // GPS_RAW_INT (msgid 24), payload 30 bytes: time_usec(8) lat(4) lon(4)
    // alt(4) eph(2) epv(2) vel(2) cog(2) fix(1) satellites(1).
    uint8_t p[30] = {0};
    put_u32(p, 0, static_cast<uint32_t>(s_t * 1e6f));
    put_i32(p, 8, static_cast<int32_t>(s_lat * 1e7f));
    put_i32(p, 12, static_cast<int32_t>(s_lon * 1e7f));
    put_i32(p, 16, static_cast<int32_t>(s_alt * 1000.0f));
    put_u16(p, 20, 70);  // eph cm
    put_u16(p, 22, 90);  // epv cm
    put_u16(p, 24, 420); // vel cm/s
    put_u16(p, 26, static_cast<uint16_t>(s_heading * 100.0f));
    p[28] = 6;           // GPS_FIX_TYPE_RTK_FIXED
    p[29] = 14;          // satellites_visible
    demo_send_frame(relay, 24, std::span<const uint8_t>(p, sizeof p));
}

static void send_attitude(UdpRelay& relay) {
    // ATTITUDE (msgid 30), payload 28 bytes.
    uint8_t p[28] = {0};
    put_u32(p, 0, static_cast<uint32_t>(s_t * 1e6f));
    put_f32(p, 4, 0.18f * sinf(s_t * 0.6f));
    put_f32(p, 8, 0.10f * sinf(s_t * 0.4f + 1.0f));
    put_f32(p, 12, s_heading * 3.14159f / 180.0f);
    put_f32(p, 16, 0.05f);
    put_f32(p, 20, 0.03f);
    put_f32(p, 24, 0.02f);
    demo_send_frame(relay, 30, p);
}

static void send_gpos(UdpRelay& relay) {
    // GLOBAL_POSITION_INT (msgid 33), full 28-byte payload: time u32, lat i32,
    // lon i32, alt i32, relative_alt i32, vx i32, vy i32, vz i32, hdg u16.
    uint8_t p[28] = {0};
    put_u32(p, 0, static_cast<uint32_t>(s_t * 1e6f));
    put_i32(p, 4, static_cast<int32_t>(s_lat * 1e7f));
    put_i32(p, 8, static_cast<int32_t>(s_lon * 1e7f));
    put_i32(p, 12, static_cast<int32_t>(s_alt * 1000.0f));
    put_i32(p, 16, static_cast<int32_t>(s_alt * 1000.0f)); // relative_alt
    put_i32(p, 20, 0);  // vx
    put_i32(p, 24, 0);  // vy
    put_i32(p, 28, 0);  // vz
    put_u16(p, 32, static_cast<uint16_t>(s_heading * 100.0f)); // hdg
    demo_send_frame(relay, 33, std::span<const uint8_t>(p, sizeof p));
}

static void send_rc(UdpRelay& relay) {
    // RC_CHANNELS (msgid 65), payload 42 bytes: time(4) + 16 channels u16 (32)
    // + chancount(1) + rssi(1) = 38... crate emits 42 with padding.
    uint8_t p[42] = {0};
    put_u32(p, 0, 0);
    uint16_t ch[8] = {1500, 1500, 1000, 1500, 1800, 1500, 1500, 1500};
    ch[2] = s_armed ? 1600 : 1000;
    for (int i = 0; i < 8; ++i) put_u16(p, 4 + i * 2, ch[i]);
    p[38] = 8;  // chancount
    p[39] = 254; // rssi
    demo_send_frame(relay, 65, std::span<const uint8_t>(p, sizeof p));
}

static void send_vfr(UdpRelay& relay) {
    // VFR_HUD (msgid 74), payload 19 bytes: airspeed(4) groundspeed(4)
    // alt(4) climb(4) heading(2) throttle(1) = 19.
    uint8_t p[19] = {0};
    put_f32(p, 0, 4.5f);
    put_f32(p, 4, 4.2f);
    put_f32(p, 8, s_alt);
    put_f32(p, 12, 0.3f);
    put_i32(p, 16, static_cast<int32_t>(s_heading)); // heading i16 at 16..18
    demo_send_frame(relay, 74, std::span<const uint8_t>(p, sizeof p));
}

static void send_battery(UdpRelay& relay) {
    // BATTERY_STATUS (msgid 147), payload 51 bytes. Field layout (from the
    // mavlink crate's 51-byte encoding): current_consumed u32(0), energy u32(4),
    // temperature i16(8), voltages u16[10](10..30), current_battery i16(30),
    // id u8(32), function u8(33), type u8(34), remaining i8(35), time_remaining
    // u32(36), charge_state u8(40), voltages_ext u16[10](41..51).
    uint8_t p[51] = {0};
    put_u16(p, 8, 25);   // temperature
    put_u16(p, 10, static_cast<uint16_t>(s_batt_v * 1000.0f)); // voltages[0]
    put_i16(p, 30, -8400); // current_battery cA
    p[32] = 0;           // id
    p[33] = 0;           // function ALL
    p[34] = 3;           // type LiPo
    p[35] = s_batt_pct;  // remaining
    p[40] = 1;           // charge_state OK
    demo_send_frame(relay, 147, std::span<const uint8_t>(p, sizeof p));
}

static void send_radio_status(UdpRelay& relay) {
    uint8_t p[9] = {0};
    put_u16(p, 0, 0);
    put_u16(p, 2, 0);
    p[4] = s_rssi;
    p[5] = s_rssi - 6;
    p[6] = 50;
    p[7] = 30;
    p[8] = 36;
    demo_send_frame(relay, 109, p);
}

static void send_statustext(UdpRelay& relay, const char* text) {
    // STATUSTEXT: severity u8 + text[50]. Fixed 51-byte payload.
    uint8_t p[51] = {0};
    p[0] = 6; // MAV_SEVERITY_INFO
    size_t n = strlen(text);
    if (n > 50) n = 50;
    std::memcpy(p + 1, text, n);
    demo_send_frame(relay, 253, std::span<const uint8_t>(p, sizeof p));
}

static void send_param_value(UdpRelay& relay, uint16_t index, const ParamDef* p) {
    uint8_t raw[25] = {0};
    put_f32(raw, 0, p->value);
    put_u16(raw, 4, kParamCount);
    put_u16(raw, 6, index);
    size_t idn = strlen(p->id);
    if (idn > 16) idn = 16;
    std::memcpy(raw + 8, p->id, idn);
    raw[24] = p->type;
    demo_send_frame(relay, 22, raw); // PARAM_VALUE
}

static void send_command_ack(UdpRelay& relay, uint16_t cmd, uint8_t result) {
    uint8_t p[10] = {0};
    put_u16(p, 0, cmd);
    p[2] = result; // MAV_RESULT_ACCEPTED = 0
    demo_send_frame(relay, 77, std::span<const uint8_t>(p, 3));
}

static void send_mission_request(UdpRelay& relay, uint8_t mtype, uint16_t seq) {
    // MISSION_REQUEST_INT (51): seq u16(0-1) target_sys(2) target_comp(3) type(4)
    // MAVLink v2 truncates trailing zero fields — when mission_type=0 (MISSION),
    // the type byte is dropped and the wire payload is 4 bytes, matching what
    // the mavlink crate emits. Non-zero mission_type keeps the 5th byte.
    uint8_t p[5] = {0};
    put_u16(p, 0, seq);
    p[2] = 1;  // target_system
    p[3] = 1;  // target_component
    p[4] = mtype;
    size_t len = (mtype != 0) ? 5 : 4;
    demo_send_frame(relay, 51, std::span<const uint8_t>(p, len));
}

static void send_mission_ack(UdpRelay& relay, uint8_t mtype, uint8_t result) {
    // MISSION_ACK (47): target_sys(0) target_comp(1) result u8(2) type(3) opaque u32(4-7)
    // v2 truncation: for result=0, type=0, opaque=0 → only 2 bytes (sys+comp).
    // But result=0 (ACCEPTED) is non-zero? No — 0 is zero. The crate emits 2 bytes
    // for ACCEPTED+MISSION type, so we match that: sys(0) comp(1) = 2 bytes when
    // result and type are both 0. For non-zero result, extend.
    uint8_t p[8] = {0};
    p[0] = 1;  // target_system
    p[1] = 1;  // target_component
    p[2] = result;
    p[3] = mtype;
    size_t len = 2;
    if (result != 0) len = 3;
    if (mtype != 0) len = 4;
    demo_send_frame(relay, 47, std::span<const uint8_t>(p, len));
}

static void send_mission_count(UdpRelay& relay, uint8_t mtype, uint16_t count) {
    // MISSION_COUNT (44): count u16(0-1) target_sys(2) target_comp(3) type(4) opaque u32(5-8)
    // v2 truncation drops trailing zeros — for mission_type=0, opaque_id=0
    // the wire payload is 4 bytes (count + sys + comp), matching the crate.
    uint8_t p[9] = {0};
    put_u16(p, 0, count);
    p[2] = 1;  // target_system
    p[3] = 1;  // target_component
    p[4] = mtype;
    // opaque_id = 0 (bytes 5-8)
    size_t len = 4;
    if (mtype != 0) len = 5;  // keep mission_type
    // opaque_id is always 0, never extends the payload
    demo_send_frame(relay, 44, std::span<const uint8_t>(p, len));
}

// ---------------------------------------------------------------------------
// Inbound command handling — the protocol responses Galapagos expects.
// ---------------------------------------------------------------------------

// COMMAND_LONG (msgid 76) payload layout per MAVLink common.xml:
//   param1 f32(0)  param2 f32(4)  param3 f32(8)  param4 f32(12)
//   param5 f32(16) param6 f32(20) param7 f32(24)
//   command u16(28)  target_system u8(30)  target_component u8(31)
//   confirmation u8(32)
// The command field is at offset 28 — NOT offset 2. Getting this wrong
// means every single command (arm, mode, takeoff, mission start) silently
// falls into the switch's default and gets an UNSUPPORTED ack.
static void handle_command_long(UdpRelay& relay, const uint8_t* p) {
    uint16_t cmd = get_u16(p + 28);
    float p1 = get_f32(p + 0);
    float p2 = get_f32(p + 4);
    switch (cmd) {
        case 400: // MAV_CMD_COMPONENT_ARM_DISARM
            s_armed = p1 >= 0.5f;
            send_command_ack(relay, cmd, 0);
            send_statustext(relay, s_armed ? "SITL: armed" : "SITL: disarmed");
            break;
        case 176: // MAV_CMD_DO_SET_MODE (param2 = custom_mode number)
            s_mode = static_cast<uint8_t>(p2);
            send_command_ack(relay, cmd, 0);
            send_statustext(relay, "SITL: mode");
            break;
        case 22: // MAV_CMD_NAV_TAKEOFF
            s_mode = 3; // Auto
            send_command_ack(relay, cmd, 0);
            send_statustext(relay, "SITL: takeoff");
            break;
        case 21: // MAV_CMD_NAV_LAND
            s_mode = 9; // Land
            send_command_ack(relay, cmd, 0);
            send_statustext(relay, "SITL: landing");
            break;
        case 20: // MAV_CMD_NAV_RETURN_TO_LAUNCH
            s_mode = 6; // RTL
            send_command_ack(relay, cmd, 0);
            send_statustext(relay, "SITL: RTL");
            break;
        case 209: // MAV_CMD_DO_MOTOR_TEST
        case 1000: // MAV_CMD_ACTUATOR_TEST
            send_command_ack(relay, cmd, 0);
            send_statustext(relay, "SITL: motor test");
            break;
        case 511: // MAV_CMD_SET_MESSAGE_INTERVAL
            send_command_ack(relay, cmd, 0);
            break;
        case 207: // MAV_CMD_DO_FENCE_ENABLE
            send_command_ack(relay, cmd, 0);
            break;
        case 300: // MAV_CMD_MISSION_START
            s_mode = 3;
            send_command_ack(relay, cmd, 0);
            send_statustext(relay, "SITL: mission start");
            break;
        default:
            send_command_ack(relay, cmd, 4); // MAV_RESULT_UNSUPPORTED
            break;
    }
}

// PARAM_REQUEST_LIST: payload = (target_sys, target_comp). Reply with the
// full table one PARAM_VALUE per frame.
static void handle_param_request_list(UdpRelay& relay) {
    for (size_t i = 0; i < kParamCount; ++i) {
        send_param_value(relay, static_cast<uint16_t>(i), &s_params[i]);
    }
}

// PARAM_REQUEST_READ (20): param_index i16(0-1) target_sys(2) target_comp(3) id[16](4-19)
static void handle_param_request_read(UdpRelay& relay, const uint8_t* p) {
    int16_t idx = static_cast<int16_t>(get_u16(p + 0));
    if (idx >= 0 && static_cast<size_t>(idx) < kParamCount) {
        send_param_value(relay, static_cast<uint16_t>(idx), &s_params[idx]);
    }
}

// PARAM_SET (23): target_sys(0) target_comp(1) value f32(2-5) id[16](6-21) type(22)
static void handle_param_set(UdpRelay& relay, const uint8_t* p) {
    char id[17];
    std::memcpy(id, p + 6, 16);
    id[16] = 0;
    for (size_t i = 0; i < kParamCount; ++i) {
        if (strncmp(s_params[i].id, id, 16) == 0) {
            s_params[i].value = get_f32(p + 2);
            s_params[i].type = p[22];
            send_param_value(relay, static_cast<uint16_t>(i), &s_params[i]);
            return;
        }
    }
}

// MISSION_COUNT (upload start): (count u16, target_sys, target_comp, type u8, opaque u32).
// We take over and request items by sequence number.
// MISSION_COUNT (44) incoming: count u16(0-1) target_sys(2) target_comp(3) type(4) opaque(5-8)
// Note: MAVLink v2 zero-truncation drops trailing zero mission_type=0 and opaque=0,
// making wire payload length 4 bytes for standard missions.
static void handle_mission_count(UdpRelay& relay, const uint8_t* p, size_t plen) {
    uint16_t count = get_u16(p + 0);
    uint8_t mtype = (plen > 4) ? p[4] : 0;
    s_mission_count = 0;
    s_mission_pending = (count <= kMaxMission) ? count : 0;
    ESP_LOGI(TAG, "MISSION_COUNT count=%u type=%u pending=%u plen=%u",
             (unsigned)count, (unsigned)mtype, (unsigned)s_mission_pending, (unsigned)plen);
    send_mission_request(relay, mtype, 0);
}

// MISSION_ITEM_INT (uploaded item): target_sys, target_comp, seq u16, frame,
// cmd u16, current, autocontinue, p1..p4 f32, x i32, y i32, z f32, type u8.
// MISSION_ITEM_INT (73) incoming layout (sorted by size):
// param1 f32(0) param2 f32(4) param3 f32(8) param4 f32(12)
// x i32(16) y i32(20) z f32(24) seq u16(28) command u16(30) target_sys(32)
// target_comp(33) frame(34) current(35) autocontinue(36) mission_type(37)
// BUT: v2 truncation drops trailing zero mission_type=0, so the wire payload
// is 37 bytes and mission_type is NOT present. Default to 0 (MISSION).
static void handle_mission_item(UdpRelay& relay, const uint8_t* p, size_t plen) {
    uint16_t seq = get_u16(p + 28);
    uint8_t mtype = (plen > 37) ? p[37] : 0;  // 0 if truncated
    ESP_LOGI(TAG, "MISSION_ITEM seq=%u type=%u pending=%u plen=%u",
             (unsigned)seq, (unsigned)mtype, (unsigned)s_mission_pending, (unsigned)plen);
    if (s_mission_pending == 0) return;
    if (seq < kMaxMission) {
        s_mission[seq].seq = static_cast<uint8_t>(seq);
        s_mission[seq].p1  = get_f32(p + 0);
        s_mission[seq].p2  = get_f32(p + 4);
        s_mission[seq].p3  = get_f32(p + 8);
        s_mission[seq].p4  = get_f32(p + 12);
        s_mission[seq].x   = get_i32(p + 16);
        s_mission[seq].y   = get_i32(p + 20);
        s_mission[seq].z   = get_f32(p + 24);
        s_mission[seq].cmd = get_u16(p + 30);
    }
    s_mission_count = seq + 1;
    if (seq + 1 < s_mission_pending) {
        send_mission_request(relay, mtype, seq + 1);
    } else {
        s_mission_pending = 0;
        send_mission_ack(relay, mtype, 0); // ACCEPTED
        send_statustext(relay, "SITL: mission stored");
    }
}

// MISSION_REQUEST_LIST (download): reply with our stored count.
static void handle_mission_request_list(UdpRelay& relay, const uint8_t* p, size_t plen) {
    uint8_t mtype = (plen > 2) ? p[2] : 0;
    send_mission_count(relay, mtype, s_mission_count);
}

// MISSION_REQUEST_INT (51) incoming: seq u16(0-1) target_sys(2) target_comp(3) type(4)
static void handle_mission_request_int(UdpRelay& relay, const uint8_t* p, size_t plen) {
    uint16_t seq = get_u16(p + 0);
    uint8_t mtype = (plen > 4) ? p[4] : 0;
    if (seq < s_mission_count) {
        const auto& m = s_mission[seq];
        uint8_t item[38] = {0};
        put_f32(item, 0, m.p1);
        put_f32(item, 4, m.p2);
        put_f32(item, 8, m.p3);
        put_f32(item, 12, m.p4);
        put_i32(item, 16, m.x);
        put_i32(item, 20, m.y);
        put_f32(item, 24, m.z);
        put_u16(item, 28, seq);
        put_u16(item, 30, m.cmd);
        item[32] = 1; // target_system
        item[33] = 1; // target_component
        item[34] = 3; // MAV_FRAME_GLOBAL_RELATIVE_ALT
        item[35] = 0; // current
        item[36] = 1; // autocontinue
        item[37] = mtype;
        size_t len = (mtype != 0) ? 38 : 37;
        demo_send_frame(relay, 73, std::span<const uint8_t>(item, len)); // MISSION_ITEM_INT (73)
    } else {
        send_mission_ack(relay, mtype, 5); // MAV_MISSION_INVALID
    }
}

// MISSION_CLEAR_ALL: (target_sys, target_comp, type u8).
static void handle_mission_clear(UdpRelay& relay, const uint8_t* p, size_t plen) {
    uint8_t mtype = (plen > 2) ? p[2] : 0;
    s_mission_count = 0;
    send_mission_ack(relay, mtype, 0);
    send_statustext(relay, "SITL: mission cleared");
}

// LOG_REQUEST_LIST: reply one LOG_ENTRY per fake log.
static void handle_log_request_list(UdpRelay& relay) {
    for (size_t i = 0; i < kLogCount; ++i) {
        uint8_t e[14] = {0};
        put_u32(e, 0, s_logs[i].t);
        put_u32(e, 4, s_logs[i].size);
        put_u16(e, 8, s_logs[i].id);
        put_u16(e, 10, kLogCount);
        put_u16(e, 12, kLogCount - 1);
        demo_send_frame(relay, 120, std::span<const uint8_t>(e, 14)); // LOG_ENTRY
    }
}

// LOG_REQUEST_DATA: (id u16, ofs u32, count u32). Stream LOG_DATA chunks of
// 90 bytes each covering [ofs, ofs+count). Galapagos requests a window at a
// time; respond until the window is served.
static void handle_log_request_data(UdpRelay& relay, const uint8_t* p) {
    uint16_t id = get_u16(p);
    uint32_t ofs = get_u32(p + 2);
    uint32_t count = get_u32(p + 6);
    uint32_t served = 0;
    while (served < count) {
        uint32_t chunk = count - served;
        if (chunk > 90) chunk = 90;
        uint8_t d[93] = {0};
        put_u16(d, 0, id);
        put_u32(d, 2, ofs + served);
        d[6] = static_cast<uint8_t>(chunk);
        // deterministic pseudo-log bytes
        for (uint32_t i = 0; i < chunk; ++i) {
            d[7 + i] = static_cast<uint8_t>((ofs + served + i) * 31 + 7);
        }
        demo_send_frame(relay, 121, std::span<const uint8_t>(d, 7 + chunk)); // LOG_DATA
        served += chunk;
    }
}

// LOG_REQUEST_END: acknowledge so Galapagos finalizes the download.
static void handle_log_request_end(UdpRelay& relay) {
    uint8_t p[2] = {0};
    demo_send_frame(relay, 122, std::span<const uint8_t>(p, 2)); // LOG_REQUEST_END
}

// ---------------------------------------------------------------------------
// Demo task
// ---------------------------------------------------------------------------

void demo_task(void*) {
    ESP_LOGI(TAG, "demo mode: synthetic ArduPilot Copter on UDP :14550");
    UdpRelay relay;
    if (!relay.init(14550)) {
        ESP_LOGE(TAG, "demo UDP bind failed");
        return;
    }

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick = 0;
    for (;;) {
        tick++;
        s_t += 0.1f;
        s_heading = s_t * 6.0f;

        // Heartbeat 1 Hz; RADIO_STATUS 1 Hz.
        if (tick % 10 == 0) {
            send_heartbeat(relay);
            send_radio_status(relay);
        }
        // Only stream heavy telemetry if a peer has connected/talked to us.
        uint32_t current_peer = s_peer_ip.load(std::memory_order_acquire);
        if (current_peer != 0) {
            // 10 Hz telemetry.
            send_sys_status(relay);
            send_attitude(relay);
            send_rc(relay);
            send_vfr(relay);
            send_battery(relay);
            // 5 Hz.
            if (tick % 2 == 0) {
                send_gps(relay);
                send_gpos(relay);
            }
        } else if (tick % 5 == 0) {
            // 2 Hz light telemetry while waiting for GCS connection
            send_sys_status(relay);
            send_attitude(relay);
            send_gps(relay);
            send_gpos(relay);
        }

        // Drain any commands the GCS sent since last tick. Learning the
        // sender's IP from each datagram also replaces the WiFi-event
        // plumbing: the moment the GCS talks to us, we know where to unicast.
        static uint32_t s_rx_log = 0;
        for (;;) {
            uint8_t buf[512];
            uint32_t peer = 0;
            size_t n = relay.recv(std::span<uint8_t>(buf, sizeof buf), 5, &peer);
            if (n == 0) break;
            if (peer != 0 && peer != s_peer_ip.load(std::memory_order_acquire)) {
                demo_set_peer(peer);
            }
            s_rx_log++;
            const uint8_t* payload;
            size_t plen;
            uint32_t msgid = demo_decode_frame(buf, n, &payload, &plen);
            ESP_LOGI(TAG, "RX#%lu msgid=%lu n=%u", (unsigned long)s_rx_log,
                     (unsigned long)msgid, (unsigned)n);
            switch (msgid) {
                case 21: handle_param_request_list(relay); break;
                case 20: handle_param_request_read(relay, payload); break;
                case 23: handle_param_set(relay, payload); break;
                case 44: handle_mission_count(relay, payload, plen); break;
                case 73: handle_mission_item(relay, payload, plen); break;
                case 43: handle_mission_request_list(relay, payload, plen); break;
                case 51: handle_mission_request_int(relay, payload, plen); break;
                case 45: handle_mission_clear(relay, payload, plen); break;
                case 117: handle_log_request_list(relay); break;
                case 119: handle_log_request_data(relay, payload); break;
                case 122: handle_log_request_end(relay); break;
                case 76: handle_command_long(relay, payload); break;
                default: break;
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}

}  // namespace bridge
