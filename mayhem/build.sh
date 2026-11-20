#!/usr/bin/env bash
# mayhem/build.sh — host build of the DS5Dongle firmware logic for fuzzing.
# The firmware targets the RP2350 (Pico SDK); there is no host build upstream. We compile the
# project's own sources (src/*.cpp, unchanged) with clang against small Pico-SDK/TinyUSB stand-ins
# in mayhem/host/include and the real BTstack headers (pinned copy in /opt/toolchains/deps).
# WDL (resampler, src/wdl_resample.cpp) and libopus come from pinned copies in /opt/toolchains/deps.
# Not built: usb_descriptors.cpp (static descriptor tables), button_functions.cpp (BOOTSEL GPIO),
# debug.cpp (ENABLE_DEBUG=0); core1_entry (opus encode/decode loop) is compiled but never started.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${BTSTACK_DIR:=/opt/toolchains/deps/btstack-1.6.2/src}"
: "${WDL_DIR:=/opt/toolchains/deps/wdl-afc0f78}"
: "${OPUS_DIR:=/opt/toolchains/deps/opus-2d862ea}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
cd "$SRC"

OUT="$SRC/mayhem/out"            # gitignored build dir (mayhem/.gitignore)
mkdir -p "$OUT/fuzz" "$OUT/test" "$SRC/mayhem/bin"

INC=(-I"$SRC/mayhem/host/include" -I"$SRC/mayhem/host" -I"$SRC/src"
     -I"$BTSTACK_DIR/src" -I"$BTSTACK_DIR/src/classic"
     -I"$WDL_DIR/src/WDL" -I"$WDL_DIR/src" -I"$OPUS_DIR/include/opus")
# Values from CMakeLists.txt for the default pico2_w build (ENABLE_WAKE_HID is always defined there).
DEFS=(-DENABLE_CLASSIC -DENABLE_SERIAL=0 -DENABLE_VERBOSE=0 -DENABLE_DEBUG=0 -DENABLE_BATT_LED=1
      -DDISABLE_SPEAKER_PROC=0 -DSYS_CLOCK_KHZ=150000 -DENABLE_WAKE_HID -DCYW43_LWIP=0
      -DWDL_RESAMPLE_TYPE=float '-DPICO_PROGRAM_VERSION_STRING="host"')
FW_SRCS=(main bt config cmd button_remap button_utils button_shortcut wake usb status_gpio battery_led
         audio elastic wdl_resample)
BT_SRCS=(btstack_util hci_cmd)

# compile <outdir> <cxxflags...>: firmware + host layer objects into <outdir>
compile() {
  local o="$1"; shift
  local pids=()
  for f in "${FW_SRCS[@]}"; do
    $CXX -std=c++20 "$@" "${INC[@]}" "${DEFS[@]}" -Dmain=ds5_firmware_main -c "src/$f.cpp" -o "$o/$f.o" & pids+=($!)
  done
  for f in ds5_host ds5_fuzz; do
    $CXX -std=c++20 "$@" "${INC[@]}" "${DEFS[@]}" -c "mayhem/host/$f.cpp" -o "$o/$f.o" & pids+=($!)
  done
  for p in "${pids[@]}"; do wait "$p"; done
}

# 1) BTstack helpers (uninstrumented dependency: little_endian_*, bd_addr_*).
for f in "${BT_SRCS[@]}"; do
  $CC -O1 $DEBUG_FLAGS -I"$SRC/src" "${INC[@]}" -DENABLE_CLASSIC -c "$BTSTACK_DIR/src/$f.c" -o "$OUT/$f.o"
done

BT_OBJS=(); for f in "${BT_SRCS[@]}"; do BT_OBJS+=("$OUT/$f.o"); done

# 2) Fuzz build: the firmware + harness layer instrumented with sanitizers + SanCov.
compile "$OUT/fuzz" $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$OUT/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"
for t in fuzz_usb_hid fuzz_bt_l2cap fuzz_session fuzz_audio; do
  $CXX -std=c++20 $SANITIZER_FLAGS $DEBUG_FLAGS -c "mayhem/$t.cpp" -o "$OUT/$t.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/$t.o" "$OUT"/fuzz/*.o "${BT_OBJS[@]}" "$OPUS_DIR/lib/libopus.a" -lm \
      "$OUT/lsan_off.o" -o "$SRC/mayhem/bin/$t"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/standalone_main.o" "$OUT/$t.o" "$OUT"/fuzz/*.o "${BT_OBJS[@]}" "$OPUS_DIR/lib/libopus.a" -lm \
      "$OUT/lsan_off.o" -o "$SRC/mayhem/bin/$t-standalone"
done

# 3) Functional self-test (known-answer checks), normal flags, independent object tree.
compile "$OUT/test" -O1 $COVERAGE_FLAGS
$CXX -std=c++20 -O1 $COVERAGE_FLAGS "${INC[@]}" "${DEFS[@]}" -c mayhem/ds5_selftest.cpp -o "$OUT/test/ds5_selftest.main.o"
$CXX $COVERAGE_FLAGS "$OUT"/test/*.o "${BT_OBJS[@]}" "$OPUS_DIR/lib/libopus.a" -lm -o "$SRC/mayhem/bin/ds5_selftest"
echo "build.sh: done"
