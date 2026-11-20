#pragma once
#include <cstdint>
#include <cstddef>
#include "btstack_defines.h"
#include "bt.h"
// Shared driver for the DS5Dongle harnesses (see ds5_fuzz.cpp).
extern bool ds5_usb_mounted, ds5_usb_suspended, ds5_usb_ready, ds5_l2cap_can_send;
extern uint32_t ds5_usb_fail_mask;
btstack_packet_handler_t ds5_get_l2cap_handler();
void ds5_reset_session();
void ds5_bt_frame(CHANNEL_TYPE ch, const uint8_t *data, size_t len);
void ds5_usb_set_report(uint8_t itf, uint8_t id, uint8_t type, const uint8_t *data, size_t len);
void ds5_usb_get_report(uint8_t itf, uint8_t id, uint8_t type, uint16_t reqlen);
void ds5_uac_request(uint8_t kind, const uint8_t *req8, const uint8_t *data, size_t len);
// Consume a little-endian field from the fuzz input; returns false when exhausted.
struct Ds5Input {
    const uint8_t *p; size_t n;
    bool u8(uint8_t &v) { if (!n) return false; v = *p++; n--; return true; }
    size_t take(const uint8_t *&out, size_t want) { if (want > n) want = n; out = p; p += want; n -= want; return want; }
};
void ds5_audio_reset();
void ds5_audio_feed(const uint8_t *pcm, size_t len);
void ds5_audio_host_read(uint32_t n);
