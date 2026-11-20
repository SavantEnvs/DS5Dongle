// DS5Dongle fuzz target: Bluetooth side: L2CAP HID interrupt (0x31 input) and control (0xA3 feature) frames from the controller.
// Input: an opcode stream run by ds5_run() (mayhem/host/ds5_fuzz.cpp) against the real firmware.
#include <cstddef>
#include <cstdint>
void ds5_run(const uint8_t *data, size_t size, const uint8_t *ops, size_t nops);
static const uint8_t kOps[] = {3, 4, 7};
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ds5_run(data, size, kOps, sizeof(kOps));
    return 0;
}
