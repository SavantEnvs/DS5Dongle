// Shared session driver for the DS5Dongle harnesses. It drives the firmware through the same
// entry points the real stacks call: TinyUSB's tud_hid_{set,get}_report_cb / tud_audio_*_cb and
// BTstack's L2CAP packet handler that bt_init() registers via l2cap_register_service().
#include <cstdlib>
#include <cstring>
#include <unordered_map>
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
#include "usb.h"
#include "wake.h"
#include "button_shortcut.h"
#include "utils.h"
#include "audio.h"
#include "battery_led.h"

void on_bt_data(CHANNEL_TYPE channel, uint8_t *data, uint16_t len);   // main.cpp
void interrupt_loop();                                                 // main.cpp
void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t, uint8_t const *, uint16_t);
uint16_t tud_hid_get_report_cb(uint8_t, uint8_t, hid_report_type_t, uint8_t *, uint16_t);
bool tud_audio_set_itf_cb(uint8_t, tusb_control_request_t const *);
bool tud_audio_get_req_entity_cb(uint8_t, tusb_control_request_t const *);
bool tud_audio_set_req_entity_cb(uint8_t, tusb_control_request_t const *, uint8_t *);
extern "C" void tud_suspend_cb(bool);
extern "C" void tud_resume_cb(void);
extern "C" void tud_mount_cb(void);
extern queue_t send_fifo;   // bt.cpp
extern std::unordered_map<uint8_t, std::vector<uint8_t> > feature_data;   // bt.cpp
extern absolute_time_t inactive_time;                                     // bt.cpp
extern uint8_t reportSeqCounter, packetCounter;                           // main.cpp
extern bool spk_active, report_dirty;                                     // main.cpp
extern USBGetStateData interrupt_in_data;                                 // main.cpp

static constexpr uint16_t CID_CONTROL = 0x0040, CID_INTERRUPT = 0x0041;

static void ds5_l2cap_event(const uint8_t *ev, size_t len) {
    std::vector<uint8_t> copy(ev, ev + len);
    ds5_get_l2cap_handler()(HCI_EVENT_PACKET, 0, copy.data(), (uint16_t) copy.size());
}
static void ds5_channel_opened(uint16_t psm, uint16_t cid) {
    uint8_t ev[27]{};   // BTstack L2CAP_EVENT_CHANNEL_OPENED layout (btstack_event.h getters)
    ev[0] = L2CAP_EVENT_CHANNEL_OPENED; ev[1] = sizeof(ev) - 2; ev[2] = 0;   // status OK
    little_endian_store_16(ev, 11, psm);
    little_endian_store_16(ev, 13, cid);
    ds5_l2cap_event(ev, sizeof(ev));
}
static void ds5_channel_closed(uint16_t cid) {
    uint8_t ev[4]{L2CAP_EVENT_CHANNEL_CLOSED, 2};
    little_endian_store_16(ev, 2, cid);
    ds5_l2cap_event(ev, sizeof(ev));
}

// Bring the firmware to a known state: defaults loaded from an erased flash, a controller
// connected on both HID channels, USB mounted. Every input starts from here.
void ds5_reset_session() {
    static bool inited = false;
    ds5_fake_now_us += 10ull * 1000 * 1000;
    ds5_usb_mounted = true; ds5_usb_suspended = false; ds5_usb_ready = true;
    ds5_usb_fail_mask = 0; ds5_l2cap_can_send = true;
    ds5_audio_reset();
    usb_keyboard_only = false; usb_reconfiguring = false;
    if (!inited) {
        inited = true;
        wake_init();
        audio_init();
        bt_init();
        bt_register_data_callback(on_bt_data);
    }
    memset(ds5_fake_flash, 0xFF, sizeof(ds5_fake_flash));
    config_load();
    // Per-input reset of the extern globals the BT paths latch: cached feature reports,
    // the DualSense Edge latch, and the inactivity timer.
    feature_data.clear();
    is_dse = false;
    inactive_time = get_absolute_time();
    // Wake FSM (state, prev button bytes, shortcut latch), UAC interface/volume state, headset flag, the
    // battery-LED blink state and the main.cpp report globals, all back to their boot values.
    wake_on_bt_disconnect();
    spk_active = false;
    set_mic_active(false);
    set_headset(false);  // plug_headset: boot value; set only on a PluggedHeadphones edge
    mute[0] = mute[1] = 0;
    volume[0] = 0.0f; volume[1] = 48.0f;
    battery_led_init();
    reportSeqCounter = 0; packetCounter = 0; report_dirty = false;
    interrupt_in_data = USBGetStateData{};
    ds5_channel_closed(CID_INTERRUPT);
    ds5_channel_closed(CID_CONTROL);
    ds5_queue_reset(&send_fifo);
    button_shortcut_reset();
    tud_mount_cb();
    ds5_channel_opened(PSM_HID_CONTROL, CID_CONTROL);
    ds5_channel_opened(PSM_HID_INTERRUPT, CID_INTERRUPT);
    // An idle, undocked input report clears on_bt_data's dock latch and the wake/shortcut edge state.
    uint8_t idle[3 + sizeof(USBGetStateData)]{};
    idle[0] = 0xA1; idle[1] = 0x31;
    idle[3] = idle[4] = idle[5] = idle[6] = 0x80; idle[3 + 7] = 0x08;
    ds5_bt_frame(INTERRUPT, idle, sizeof(idle));
    ds5_queue_reset(&send_fifo);
}

// One L2CAP SDU as BTstack hands it to the registered handler: an exact-size buffer, so
// ASan flags any read past the frame the controller actually sent.
void ds5_bt_frame(CHANNEL_TYPE ch, const uint8_t *data, size_t len) {
    uint8_t *buf = (uint8_t *) malloc(len ? len : 1);
    if (len) memcpy(buf, data, len);
    ds5_get_l2cap_handler()(L2CAP_DATA_PACKET, ch == INTERRUPT ? CID_INTERRUPT : CID_CONTROL, buf, (uint16_t) len);
    free(buf);
}

// TinyUSB delivers SET_REPORT / interrupt-OUT data of at most CFG_TUD_HID_EP_BUFSIZE bytes.
void ds5_usb_set_report(uint8_t itf, uint8_t id, uint8_t type, const uint8_t *data, size_t len) {
    if (len > CFG_TUD_HID_EP_BUFSIZE) len = CFG_TUD_HID_EP_BUFSIZE;
    uint8_t *buf = (uint8_t *) malloc(len ? len : 1);
    if (len) memcpy(buf, data, len);
    tud_hid_set_report_cb(itf, id, (hid_report_type_t) (type & 3), buf, (uint16_t) len);
    free(buf);
}

// TinyUSB's GET_REPORT buffer holds reqlen (<= CFG_TUD_HID_EP_BUFSIZE) bytes.
void ds5_usb_get_report(uint8_t itf, uint8_t id, uint8_t type, uint16_t reqlen) {
    if (reqlen > CFG_TUD_HID_EP_BUFSIZE) reqlen = CFG_TUD_HID_EP_BUFSIZE;
    uint8_t *buf = (uint8_t *) malloc(reqlen ? reqlen : 1);
    uint16_t n = tud_hid_get_report_cb(itf, id, (hid_report_type_t) (type & 3), buf, reqlen);
    if (n > reqlen) abort();   // the callback must not claim more than the buffer holds
    free(buf);
}

// UAC1 class request to an entity / interface; the data stage holds exactly wLength bytes.
void ds5_uac_request(uint8_t kind, const uint8_t *req8, const uint8_t *data, size_t len) {
    tusb_control_request_t req;
    memcpy(&req, req8, sizeof(req));
    if (req.wLength > 64) req.wLength = 64;   // CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ bound
    std::vector<uint8_t> buf(req.wLength ? req.wLength : 1, 0);
    memcpy(buf.data(), data, len < req.wLength ? len : req.wLength);
    switch (kind % 3) {
        case 0: tud_audio_set_req_entity_cb(0, &req, req.wLength ? buf.data() : nullptr); break;
        case 1: tud_audio_get_req_entity_cb(0, &req); break;
        default: tud_audio_set_itf_cb(0, &req); break;
    }
}

// Run an opcode stream. `ops` limits which opcodes this harness exercises.
//  0 USB SET_REPORT   itf id type len data[len]
//  1 USB GET_REPORT   itf id type reqlen
//  2 UAC request      kind req[8] data[wLength]
//  3 BT interrupt SDU len16 data[len]
//  4 BT control SDU   len16 data[len]
//  5 time + main loop ms8   (advance clock ms*16, run wake/shortcut tasks and interrupt_loop)
//  6 USB bus state    bits  (mounted/suspended/ready/report-failure mask, suspend/resume/mount cbs)
//  7 L2CAP CAN_SEND_NOW     (drains the firmware's BT send FIFO)
//  8 USB speaker PCM  len16 pcm[len] (4-ch 16-bit frames; audio_loop() reads them in 384-byte pulls)
//  9 USB mic IN       alt rd16 (host opens/closes the mic interface, reads rd bytes, audio_loop() runs)
void ds5_run(const uint8_t *data, size_t size, const uint8_t *ops, size_t nops) {
    ds5_reset_session();
    Ds5Input in{data, size};
    uint8_t sel;
    while (in.u8(sel)) {
        const uint8_t op = ops[sel % nops];
        uint8_t a = 0, b = 0, c = 0, d = 0;
        const uint8_t *p = nullptr;
        switch (op) {
            case 0: {
                if (!in.u8(a) || !in.u8(b) || !in.u8(c) || !in.u8(d)) return;
                size_t n = in.take(p, d % (CFG_TUD_HID_EP_BUFSIZE + 1));
                ds5_usb_set_report(a & 1, b, c, p, n);
                break;
            }
            case 1:
                if (!in.u8(a) || !in.u8(b) || !in.u8(c) || !in.u8(d)) return;
                ds5_usb_get_report(a & 1, b, c, d % (CFG_TUD_HID_EP_BUFSIZE + 1));
                break;
            case 2: {
                const uint8_t *req;
                if (!in.u8(a) || in.take(req, 8) < 8) return;
                uint16_t wlen = (uint16_t) (req[6] | (req[7] << 8));
                size_t n = in.take(p, wlen > 64 ? 64 : wlen);
                ds5_uac_request(a, req, p, n);
                break;
            }
            case 3: case 4: {
                if (!in.u8(a) || !in.u8(b)) return;
                size_t n = in.take(p, ((size_t) a | ((size_t) b << 8)) % 673);   // MTU 672
                ds5_bt_frame(op == 3 ? INTERRUPT : CONTROL, p, n);
                break;
            }
            case 5:
                if (!in.u8(a)) return;
                ds5_fake_now_us += (uint64_t) a * 16 * 1000;
                wake_task();
                button_shortcut_task();
                interrupt_loop();
                break;
            case 6: {
                if (!in.u8(a)) return;
                const bool was_suspended = ds5_usb_suspended;
                ds5_usb_mounted = a & 1; ds5_usb_suspended = (a >> 1) & 1; ds5_usb_ready = (a >> 2) & 1;
                ds5_usb_fail_mask = (a >> 3) & 7;
                if (ds5_usb_suspended && !was_suspended) tud_suspend_cb((a >> 6) & 1);
                if (!ds5_usb_suspended && was_suspended) tud_resume_cb();
                if ((a >> 7) & 1) tud_mount_cb();
                break;
            }
            case 8: {
                if (!in.u8(a) || !in.u8(b)) return;
                size_t n = in.take(p, ((size_t) a | ((size_t) b << 8)) % 1537);
                std::vector<uint8_t> pcm(p, p + n);
                ds5_audio_feed(pcm.data(), pcm.size());
                for (int i = 0; i < 8 && tud_audio_available(); i++) { audio_loop(); ds5_fake_now_us += 1000; }
                audio_loop();
                ds5_audio_feed(nullptr, 0);
                break;
            }
            case 9: {
                if (!in.u8(a) || !in.u8(b) || !in.u8(c)) return;
                uint8_t req[8] = {0x01, 0x0b, a, 0, 2, 0, 0, 0};   // SET_INTERFACE(itf 2 = mic, alt a)
                tusb_control_request_t r; memcpy(&r, req, sizeof(r));
                tud_audio_set_itf_cb(0, &r);
                ds5_audio_host_read((uint32_t) b | ((uint32_t) c << 8));
                ds5_fake_now_us += (uint64_t) (b & 0x1f) * 1000;
                audio_loop();
                break;
            }
            case 7: {
                uint8_t ev[4]{L2CAP_EVENT_CAN_SEND_NOW, 2};
                little_endian_store_16(ev, 2, CID_INTERRUPT);
                ds5_l2cap_event(ev, sizeof(ev));
                break;
            }
        }
    }
}
