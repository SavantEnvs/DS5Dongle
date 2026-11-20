// DS5Dongle fuzz target: USB host side: HID SET/GET_REPORT (incl. the 0xF6-0xFB config commands and output report 0x02) and UAC1 control requests.
// Input: an opcode stream run by ds5_run() (mayhem/host/ds5_fuzz.cpp) against the real firmware.
#include <cstddef>
#include <cstdint>
void ds5_run(const uint8_t *data, size_t size, const uint8_t *ops, size_t nops);
static const uint8_t kOps[] = {0, 1, 2};
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ds5_run(data, size, kOps, sizeof(kOps));
    return 0;
}
