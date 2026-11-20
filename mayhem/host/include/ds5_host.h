// Host (x86-64) stand-ins for the Pico SDK and TinyUSB APIs DS5Dongle's firmware calls.
// The project's own sources compile unchanged against these; BTstack uses its real headers.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#ifndef __not_in_flash_func
#define __not_in_flash_func(f) f
#endif
#define __no_inline_not_in_flash_func(f) f
#define __not_in_flash(group)
#define __time_critical_func(f) f
#define __in_flash(group)

#ifdef __cplusplus
extern "C" {
#endif

// ---- pico/time ----
typedef uint64_t absolute_time_t;
#define nil_time ((absolute_time_t) 0)
extern uint64_t ds5_fake_now_us;          // fake monotonic clock (µs), advanced by the harness
static inline uint64_t time_us_64(void) { return ds5_fake_now_us; }
static inline uint32_t time_us_32(void) { return (uint32_t) ds5_fake_now_us; }
static inline absolute_time_t get_absolute_time(void) { return ds5_fake_now_us; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t) (t / 1000); }
static inline uint64_t to_us_since_boot(absolute_time_t t) { return t; }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return ds5_fake_now_us + (uint64_t) ms * 1000; }
static inline absolute_time_t make_timeout_time_us(uint64_t us) { return ds5_fake_now_us + us; }
static inline absolute_time_t delayed_by_ms(absolute_time_t t, uint32_t ms) { return t + (uint64_t) ms * 1000; }
static inline absolute_time_t delayed_by_us(absolute_time_t t, uint64_t us) { return t + us; }
static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) { return (int64_t) (to - from); }
static inline bool time_reached(absolute_time_t t) { return ds5_fake_now_us >= t; }
static inline bool is_nil_time(absolute_time_t t) { return t == 0; }
// Busy-wait primitives advance the fake clock so firmware timeouts expire.
static inline void sleep_ms(uint32_t ms) { ds5_fake_now_us += (uint64_t) ms * 1000; }
static inline void sleep_us(uint64_t us) { ds5_fake_now_us += us; }
static inline void busy_wait_us(uint64_t us) { ds5_fake_now_us += us; }
static inline void busy_wait_ms(uint32_t ms) { ds5_fake_now_us += (uint64_t) ms * 1000; }
static inline void tight_loop_contents(void) { ds5_fake_now_us += 100; }

// ---- cyw43 / board ----
#define CYW43_WL_GPIO_LED_PIN 0
static inline int cyw43_arch_init(void) { return 0; }
static inline void cyw43_arch_gpio_put(unsigned, bool) {}
static inline bool cyw43_arch_gpio_get(unsigned) { return false; }
void cyw43_arch_poll(void);   // ds5_host.cpp: advances the clock
static inline void board_init(void) {}
static inline void board_init_after_tusb(void) {}
static inline void board_led_write(bool) {}
static inline uint32_t board_button_read(void) { return 0; }
static inline size_t board_get_unique_id(uint8_t *id, size_t max) { for (size_t i = 0; i < max; i++) id[i] = (uint8_t) i; return max; }
static inline size_t board_usb_get_serial(uint16_t *s, size_t max) { for (size_t i = 0; i < max; i++) s[i] = '0'; return max; }
#define BOARD_TUD_RHPORT 0

// ---- hardware ----
#define PICO_OK 0
#define PICO_RP2350 1
#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
#define PICO_FLASH_SIZE_BYTES (64u * 1024u)
#define PICO_FLASH_BANK_TOTAL_SIZE (FLASH_SECTOR_SIZE * 2u)
#define PICO_FLASH_BANK_STORAGE_OFFSET (PICO_FLASH_SIZE_BYTES - PICO_FLASH_BANK_TOTAL_SIZE)
extern uint8_t ds5_fake_flash[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE ((uintptr_t) ds5_fake_flash)
void flash_range_erase(uint32_t off, size_t count);
void flash_range_program(uint32_t off, const uint8_t *data, size_t count);
static inline int flash_safe_execute(void (*fn)(void *), void *param, uint32_t) { fn(param); return PICO_OK; }
static inline bool flash_safe_execute_core_init(void) { return true; }
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t) {}
static inline void watchdog_enable(uint32_t, bool) {}
static inline void watchdog_update(void) {}
static inline bool watchdog_caused_reboot(void) { return false; }
static inline void watchdog_reboot(uint32_t, uint32_t, uint32_t) {}
static inline void reset_usb_boot(uint32_t, uint32_t) {}
static inline void rom_reset_usb_boot(uint32_t, uint32_t) {}
#define VREG_VOLTAGE_1_20 0
#define VREG_VOLTAGE_DEFAULT 0
static inline void vreg_set_voltage(int) {}
static inline bool set_sys_clock_khz(uint32_t, bool) { return true; }
static inline void gpio_init(unsigned) {}
static inline void gpio_set_dir(unsigned, bool) {}
static inline void gpio_put(unsigned, bool) {}
static inline bool gpio_get(unsigned) { return false; }
static inline void gpio_pull_up(unsigned) {}
static inline void gpio_pull_down(unsigned) {}
static inline void gpio_disable_pulls(unsigned) {}
#define GPIO_OUT 1
#define GPIO_IN 0

// ---- pico/sync, pico/util/queue (single-threaded host) ----
typedef struct { int dummy; } critical_section_t;
static inline void critical_section_init(critical_section_t *) {}
static inline void critical_section_enter_blocking(critical_section_t *) {}
static inline void critical_section_exit(critical_section_t *) {}
typedef struct { uint8_t *data; uint32_t element_size, capacity, head, count; } queue_t;
void queue_init(queue_t *q, unsigned element_size, unsigned count);
bool queue_try_add(queue_t *q, const void *e);
bool queue_try_remove(queue_t *q, void *e);
bool queue_try_peek(queue_t *q, void *e);
void queue_add_blocking(queue_t *q, const void *e);
void queue_remove_blocking(queue_t *q, void *e);
static inline unsigned queue_get_level(queue_t *q) { return q->count; }
static inline bool queue_is_empty(queue_t *q) { return q->count == 0; }
static inline bool queue_is_full(queue_t *q) { return q->count == q->capacity; }
void ds5_queue_reset(queue_t *q);
static inline void multicore_launch_core1(void (*)(void)) {}
static inline void multicore_reset_core1(void) {}
static inline unsigned get_core_num(void) { return 0; }

// ---- TinyUSB ----
#define TU_ATTR_PACKED __attribute__((packed))
#define TU_U16_HIGH(u16) ((uint8_t) (((u16) >> 8) & 0x00ff))
#define TU_U16_LOW(u16) ((uint8_t) ((u16) & 0x00ff))
static inline uint8_t tu_u16_high(uint16_t u) { return TU_U16_HIGH(u); }
static inline uint8_t tu_u16_low(uint16_t u) { return TU_U16_LOW(u); }
#define TU_VERIFY(cond, ...) do { if (!(cond)) return false; } while (0)
#define TU_ASSERT(cond, ...) do { if (!(cond)) return false; } while (0)
#define TU_LOG1(...) do {} while (0)
#define TU_LOG2(...) do {} while (0)
#define TU_BREAKPOINT() do {} while (0)
#define TU_ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
typedef struct TU_ATTR_PACKED {
    union { struct TU_ATTR_PACKED { uint8_t recipient : 5; uint8_t type : 2; uint8_t direction : 1; } bmRequestType_bit; uint8_t bmRequestType; };
    uint8_t bRequest; uint16_t wValue; uint16_t wIndex; uint16_t wLength;
} tusb_control_request_t;
typedef enum { HID_REPORT_TYPE_INVALID = 0, HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_OUTPUT, HID_REPORT_TYPE_FEATURE } hid_report_type_t;
typedef enum { TUSB_ROLE_INVALID = 0, TUSB_ROLE_DEVICE = 1, TUSB_ROLE_HOST = 2 } tusb_role_t;
typedef enum { TUSB_SPEED_FULL = 0, TUSB_SPEED_LOW = 1, TUSB_SPEED_HIGH = 2, TUSB_SPEED_AUTO = 0xff } tusb_speed_t;
typedef struct { tusb_role_t role; tusb_speed_t speed; } tusb_rhport_init_t;
enum { AUDIO10_FU_CTRL_MUTE = 0x01, AUDIO10_FU_CTRL_VOLUME = 0x02 };
enum { AUDIO10_CS_REQ_SET_CUR = 0x01, AUDIO10_CS_REQ_GET_CUR = 0x81, AUDIO10_CS_REQ_GET_MIN = 0x82,
       AUDIO10_CS_REQ_GET_MAX = 0x83, AUDIO10_CS_REQ_GET_RES = 0x84 };
static inline bool tusb_init(uint8_t, const tusb_rhport_init_t *) { return true; }
static inline void tud_task(void) {}
extern bool ds5_usb_mounted, ds5_usb_suspended, ds5_usb_ready;
static inline bool tud_mounted(void) { return ds5_usb_mounted; }
static inline bool tud_suspended(void) { return ds5_usb_suspended; }
static inline bool tud_connect(void) { return true; }
static inline bool tud_disconnect(void) { return true; }
static inline bool tud_remote_wakeup(void) { return ds5_usb_suspended; }
static inline void dcd_remote_wakeup(uint8_t) {}
bool tud_hid_n_ready(uint8_t instance);
bool tud_hid_n_report(uint8_t instance, uint8_t report_id, const void *report, uint16_t len);
bool tud_hid_n_keyboard_report(uint8_t instance, uint8_t report_id, uint8_t modifier, const uint8_t keycode[6]);
static inline bool tud_hid_ready(void) { return tud_hid_n_ready(0); }
static inline bool tud_hid_report(uint8_t id, const void *r, uint16_t len) { return tud_hid_n_report(0, id, r, len); }
bool tud_audio_buffer_and_schedule_control_xfer(uint8_t rhport, const tusb_control_request_t *req, void *data, uint16_t len);
// UAC streaming: the speaker OUT FIFO hands out host PCM the harness queued; the mic IN FIFO
// is a byte counter the harness drains (the host reading).
typedef struct { uint32_t count, depth; } tu_fifo_t;
static inline uint16_t tu_fifo_count(tu_fifo_t *f) { return (uint16_t) f->count; }
static inline uint16_t tu_fifo_remaining(tu_fifo_t *f) { return (uint16_t) (f->depth - f->count); }
tu_fifo_t *tud_audio_get_ep_in_ff(void);
uint16_t tud_audio_get_ep_in_fifo_threshold(void);
uint16_t tud_audio_available(void);
uint16_t tud_audio_read(void *buf, uint16_t len);
uint16_t tud_audio_write(const void *data, uint16_t len);
static inline void multicore_launch_core1_with_stack(void (*)(void), uint32_t *, size_t) {}

// ---- pico/time alarms (status_gpio.cpp) ----
typedef unsigned int uint;
typedef int32_t alarm_id_t;
typedef int64_t (*alarm_callback_t)(alarm_id_t id, void *user_data);
static inline alarm_id_t add_alarm_in_ms(uint32_t, alarm_callback_t, void *, bool) { return 1; }
static inline bool cancel_alarm(alarm_id_t) { return true; }
#define NUM_BANK0_GPIOS 48

#ifdef __cplusplus
}
#endif

// The firmware's own TinyUSB configuration (CFG_TUD_HID_EP_BUFSIZE, ...).
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU 0
#endif
#include "tusb_config.h"
