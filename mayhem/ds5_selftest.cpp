// Known-answer tests for the DS5Dongle firmware's USB<->BT bridge logic, run on the host against
// the real sources (built with normal flags by build.sh, run by test.sh). Prints PASS/FAIL per case.
#include <cstdio>
#include <cstring>
#include <vector>
#include "ds5_host.h"
#include "ds5_fuzz.h"
#include "btstack_event.h"
#include "btstack_tlv.h"
#include "gap.h"
#include "hci.h"
#include "l2cap.h"
#include "btstack_util.h"
#include "config.h"
#include "utils.h"

extern bool ds5_capture;
extern std::vector<std::vector<uint8_t>> ds5_l2cap_sent;
extern std::vector<uint8_t> ds5_last_hid_report;
uint16_t tud_hid_get_report_cb(uint8_t, uint8_t, hid_report_type_t, uint8_t *, uint16_t);
void interrupt_loop();

static int failed = 0;
static void check(const char *name, bool ok) { printf("%s %s\n", ok ? "PASS" : "FAIL", name); if (!ok) failed++; }

static std::vector<uint8_t> get_report(uint8_t id) {
    uint8_t buf[64]; memset(buf, 0xEE, sizeof(buf));
    uint16_t n = tud_hid_get_report_cb(0, id, HID_REPORT_TYPE_FEATURE, buf, sizeof(buf));
    return std::vector<uint8_t>(buf, buf + n);
}
static void can_send_now() {
    uint8_t ev[4]{L2CAP_EVENT_CAN_SEND_NOW, 2, 0x41, 0x00};
    ds5_get_l2cap_handler()(HCI_EVENT_PACKET, 0, ev, sizeof(ev));
}
// Independent bitwise CRC-32 (IEEE, reflected), as the DualSense expects over 0xA2 + report.
static uint32_t crc32_ref(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}

int main() {
    ds5_capture = true;

    ds5_reset_session();
    auto v = get_report(0xF8);
    check("firmware_version_report", std::string(v.begin(), v.end()) == PICO_PROGRAM_VERSION_STRING);

    ds5_reset_session();
    auto cfg = get_report(0xF7);
    bool ok = cfg.size() == sizeof(Config_body);
    const size_t prm = offsetof(Config_body, polling_rate_mode);
    if (ok) {
        uint8_t set[1 + sizeof(Config_body)];
        set[0] = 0x01; memcpy(set + 1, cfg.data(), cfg.size());
        set[1 + prm] = cfg[prm] == 2 ? 1 : 2;
        ds5_usb_set_report(0, 0xF6, HID_REPORT_TYPE_FEATURE, set, sizeof(set));
        auto cfg2 = get_report(0xF7);
        ok = cfg2.size() == cfg.size() && cfg2[prm] == set[1 + prm];
        for (size_t i = 0; ok && i < cfg.size(); i++) if (i != prm && cfg2[i] != cfg[i]) ok = false;
        // 0x02 persists to flash; a fresh session reloads it.
        uint8_t save = 0x02;
        ds5_usb_set_report(0, 0xF6, HID_REPORT_TYPE_FEATURE, &save, 1);
        config_load();
        auto cfg3 = get_report(0xF7);
        ok = ok && cfg3.size() == cfg.size() && cfg3[prm] == set[1 + prm];
    }
    check("config_set_get_save_roundtrip", ok);

    ds5_reset_session();
    ds5_l2cap_sent.clear();
    uint8_t out[48]{}; out[0] = 0x02; out[1] = 0x03; out[3] = 0x40; out[44] = 0xAB;
    ds5_usb_set_report(0, 0, HID_REPORT_TYPE_INVALID, out, sizeof(out));
    can_send_now();
    ok = ds5_l2cap_sent.size() == 1;
    if (ok) {
        const auto &f = ds5_l2cap_sent[0];
        ok = f.size() == 79 && f[0] == 0xA2 && f[1] == 0x31 && f[3] == 0x10 && f[4] == 0x03 && f[6] == 0x40;
        if (ok) {
            const uint32_t crc = f[75] | (f[76] << 8) | (f[77] << 16) | ((uint32_t) f[78] << 24);
            ok = crc == crc32_ref(f.data(), 75);
        }
    }
    check("output_report_forwarded_with_crc", ok);

    ds5_reset_session();
    uint8_t feat[42]; feat[0] = 0xA3; feat[1] = 0x05;
    for (int i = 0; i < 40; i++) feat[2 + i] = (uint8_t) (i * 7 + 1);
    ds5_bt_frame(CONTROL, feat, sizeof(feat));
    auto f5 = get_report(0x05);
    check("feature_report_cached_from_bt", f5.size() == 40 && memcmp(f5.data(), feat + 2, 40) == 0);

    ds5_reset_session();
    uint8_t in[3 + sizeof(USBGetStateData)]{};
    in[0] = 0xA1; in[1] = 0x31; in[3] = 0x12; in[4] = 0x34; in[5] = 0x80; in[6] = 0x80; in[3 + 7] = 0x08;
    ds5_last_hid_report.clear();
    ds5_bt_frame(INTERRUPT, in, sizeof(in));
    interrupt_loop();
    check("bt_input_reaches_usb_report", ds5_last_hid_report.size() == sizeof(USBGetStateData) &&
                                         ds5_last_hid_report[0] == 0x12 && ds5_last_hid_report[1] == 0x34);

    printf("SUMMARY passed=%d failed=%d\n", 5 - failed, failed);
    return failed ? 1 : 0;
}
