// DS5Dongle fuzz target: Both sides interleaved with main-loop ticks: host-set config/remaps/shortcuts applied to controller input.
// Input: an opcode stream run by ds5_run() (mayhem/host/ds5_fuzz.cpp) against the real firmware.
#include <cstddef>
#include <cstdint>
void ds5_run(const uint8_t *data, size_t size, const uint8_t *ops, size_t nops);
static const uint8_t kOps[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ds5_run(data, size, kOps, sizeof(kOps));
    return 0;
}
