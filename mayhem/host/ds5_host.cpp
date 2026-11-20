// Host implementations of the platform layer (Pico SDK, TinyUSB device, BTstack L2CAP/GAP/HCI
// entry points) that DS5Dongle's firmware calls. Only the platform is faked here: every
// project source (main/bt/config/cmd/button_*/wake/usb/status_gpio/battery_led) is the real one.
#include <cstdlib>
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
#include "hci_dump.h"
#include "classic/sdp_server.h"

uint64_t ds5_fake_now_us = 1000000;
uint8_t ds5_fake_flash[PICO_FLASH_SIZE_BYTES];
bool ds5_usb_mounted = true, ds5_usb_suspended = false, ds5_usb_ready = true;
uint32_t ds5_usb_fail_mask = 0;   // fuzz-chosen: make tud_hid_n_report fail for an instance

void cyw43_arch_poll(void) { ds5_fake_now_us += 1000; }

void flash_range_erase(uint32_t off, size_t count) {
    if (off > sizeof(ds5_fake_flash) || count > sizeof(ds5_fake_flash) - off) abort();
    memset(ds5_fake_flash + off, 0xFF, count);
}
void flash_range_program(uint32_t off, const uint8_t *data, size_t count) {
    if (off > sizeof(ds5_fake_flash) || count > sizeof(ds5_fake_flash) - off) abort();
    for (size_t i = 0; i < count; i++) ds5_fake_flash[off + i] &= data[i];
}

// ---- queue_t: single-threaded ring buffer ----
void queue_init(queue_t *q, unsigned element_size, unsigned count) {
    free(q->data);
    q->data = (uint8_t *) calloc(count ? count : 1, element_size);
    q->element_size = element_size; q->capacity = count; q->head = 0; q->count = 0;
}
void ds5_queue_reset(queue_t *q) { q->head = 0; q->count = 0; }
bool queue_try_add(queue_t *q, const void *e) {
    if (q->count == q->capacity) return false;
    memcpy(q->data + ((q->head + q->count) % q->capacity) * q->element_size, e, q->element_size);
    q->count++;
    return true;
}
bool queue_try_peek(queue_t *q, void *e) {
    if (!q->count) return false;
    if (e) memcpy(e, q->data + q->head * q->element_size, q->element_size);
    return true;
}
bool queue_try_remove(queue_t *q, void *e) {
    if (!queue_try_peek(q, e)) return false;
    q->head = (q->head + 1) % q->capacity; q->count--;
    return true;
}
void queue_add_blocking(queue_t *q, const void *e) { queue_try_add(q, e); }
void queue_remove_blocking(queue_t *q, void *e) { queue_try_remove(q, e); }

// ---- TinyUSB device side ----
static volatile uint32_t ds5_sink;
bool ds5_capture = false;                          // self-test only: record what the firmware sends
std::vector<std::vector<uint8_t>> ds5_l2cap_sent;
std::vector<uint8_t> ds5_last_hid_report;
static void ds5_consume(const void *p, size_t n) {
    // Read every byte the firmware hands to the USB stack, so ASan sees an over-read at the source.
    const uint8_t *b = (const uint8_t *) p; uint32_t h = 0;
    for (size_t i = 0; i < n; i++) h = h * 31 + b[i];
    ds5_sink = h;
}
bool tud_hid_n_ready(uint8_t instance) { return ds5_usb_ready && !((ds5_usb_fail_mask >> (instance & 7)) & 1); }
bool tud_hid_n_report(uint8_t instance, uint8_t, const void *report, uint16_t len) {
    if (len > CFG_TUD_HID_EP_BUFSIZE) abort();   // TinyUSB would reject; the firmware must not ask
    ds5_consume(report, len);
    if (ds5_capture && instance == 0) ds5_last_hid_report.assign((const uint8_t *) report, (const uint8_t *) report + len);
    return tud_hid_n_ready(instance);
}
bool tud_hid_n_keyboard_report(uint8_t instance, uint8_t, uint8_t, const uint8_t keycode[6]) {
    if (keycode) ds5_consume(keycode, 6);
    return tud_hid_n_ready(instance);
}
bool tud_audio_buffer_and_schedule_control_xfer(uint8_t, const tusb_control_request_t *req, void *data, uint16_t len) {
    ds5_consume(data, len);
    return req != nullptr;
}

// ---- UAC streaming endpoints ----
static tu_fifo_t ds5_mic_ff = {0, 1024};
static const uint8_t *ds5_spk_data = nullptr;
static size_t ds5_spk_len = 0;
void ds5_audio_reset() { ds5_mic_ff.count = 0; ds5_spk_data = nullptr; ds5_spk_len = 0; }
void ds5_audio_feed(const uint8_t *pcm, size_t len) { ds5_spk_data = pcm; ds5_spk_len = len; }
void ds5_audio_host_read(uint32_t n) { ds5_mic_ff.count = n < ds5_mic_ff.count ? ds5_mic_ff.count - n : 0; }
tu_fifo_t *tud_audio_get_ep_in_ff(void) { return &ds5_mic_ff; }
uint16_t tud_audio_get_ep_in_fifo_threshold(void) { return 384; }
uint16_t tud_audio_available(void) { return (uint16_t) ds5_spk_len; }
uint16_t tud_audio_read(void *buf, uint16_t len) {
    const uint16_t n = (uint16_t) (ds5_spk_len < len ? ds5_spk_len : len);
    memcpy(buf, ds5_spk_data, n);
    ds5_spk_data += n; ds5_spk_len -= n;
    return n;
}
uint16_t tud_audio_write(const void *data, uint16_t len) {
    ds5_consume(data, len);
    const uint16_t n = (uint16_t) (len < ds5_mic_ff.depth - ds5_mic_ff.count ? len : ds5_mic_ff.depth - ds5_mic_ff.count);
    ds5_mic_ff.count += n;
    return n;
}
void button_check() {}

// ---- BTstack: L2CAP / SDP / GAP / HCI entry points the firmware calls ----
static btstack_packet_handler_t ds5_l2cap_handler = nullptr;
btstack_packet_handler_t ds5_get_l2cap_handler() { return ds5_l2cap_handler; }
bool ds5_l2cap_can_send = true;
void l2cap_add_event_handler(btstack_packet_callback_registration_t *r) { ds5_l2cap_handler = r->callback; }
uint8_t l2cap_register_service(btstack_packet_handler_t h, uint16_t, uint16_t, gap_security_level_t) {
    ds5_l2cap_handler = h; return 0;
}
void l2cap_init(void) {}
void sdp_init(void) {}
uint8_t l2cap_send(uint16_t, const uint8_t *data, uint16_t len) {
    if (len > 672) abort();   // MTU the firmware registered (MTU_CONTROL/MTU_INTERRUPT)
    ds5_consume(data, len);
    if (ds5_capture) ds5_l2cap_sent.emplace_back(data, data + len);
    return 0;
}
bool l2cap_can_send_packet_now(uint16_t) { return ds5_l2cap_can_send; }
uint8_t l2cap_request_can_send_now_event(uint16_t) { return 0; }
uint16_t l2cap_get_remote_mtu_for_local_cid(uint16_t) { return 672; }
uint8_t l2cap_create_channel(btstack_packet_handler_t, bd_addr_t, uint16_t, uint16_t, uint16_t *out) { if (out) *out = 0; return 0; }
void l2cap_accept_connection(uint16_t) {}
void btstack_tlv_get_instance(const btstack_tlv_t **impl, void **ctx) { *impl = nullptr; *ctx = nullptr; }
void hci_add_event_handler(btstack_packet_callback_registration_t *) {}
int hci_power_control(HCI_POWER_MODE) { return 0; }
void hci_set_inquiry_mode(inquiry_mode_t) {}
uint8_t hci_send_cmd(const hci_cmd_t *, ...) { return 0; }
void gap_ssp_set_enable(int) {}
void gap_secure_connections_enable(bool) {}
void gap_ssp_set_io_capability(int) {}
void gap_ssp_set_authentication_requirement(int) {}
void gap_connectable_control(uint8_t) {}
void gap_discoverable_control(uint8_t) {}
int gap_read_rssi(hci_con_handle_t) { return 0; }
int gap_inquiry_start(uint8_t) { return 0; }
int gap_inquiry_stop(void) { return 0; }
uint16_t hci_usable_acl_packet_types(void) { return 0xcc18; }
void gap_set_page_scan_activity(uint16_t, uint16_t) {}
void gap_set_page_scan_type(page_scan_type_t) {}
void gap_drop_link_key_for_bd_addr(bd_addr_t) {}
void gap_delete_all_link_keys(void) {}
bool gap_get_link_key_for_bd_addr(bd_addr_t, link_key_t, link_key_type_t *) { return false; }
int gap_link_key_iterator_init(btstack_link_key_iterator_t *) { return 0; }
int gap_link_key_iterator_get_next(btstack_link_key_iterator_t *, bd_addr_t, link_key_t, link_key_type_t *) { return 0; }
void gap_link_key_iterator_done(btstack_link_key_iterator_t *) {}
int gap_pin_code_response(const bd_addr_t, const char *) { return 0; }
void hci_dump_log(int, const char *, ...) {}
