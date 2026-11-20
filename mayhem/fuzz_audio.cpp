// DS5Dongle fuzz target: USB speaker/haptics PCM from the host through audio_loop() (4-channel split,
// 48k->3k haptics resample, BT 0x32/0x36/0x39 report building), with host-set config (0xF6) and
// mic-interface open/close + host reads in between.
// Input: an opcode stream run by ds5_run() (mayhem/host/ds5_fuzz.cpp) against the real firmware.
#include <cstddef>
#include <cstdint>
void ds5_run(const uint8_t *data, size_t size, const uint8_t *ops, size_t nops);
static const uint8_t kOps[] = {8, 8, 9, 0, 7, 5};
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ds5_run(data, size, kOps, sizeof(kOps));
    return 0;
}
