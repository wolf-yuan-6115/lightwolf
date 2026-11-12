# Fixed-point EQ benchmarks

This experimental tool compares Pumper's float EQ with a double reference and
two Q31 implementations. It does not change the default playback implementation.
All firmware images continue to report version 5.1.

## Host requirements and tests

Use CMake, a C11 compiler supporting `__int128` (GCC or Clang), and Python 3.

```sh
cmake -S firmware/benchmarks -B /tmp/pumper-fixed-host -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/pumper-fixed-host -j
ctest --test-dir /tmp/pumper-fixed-host --output-on-failure
/tmp/pumper-fixed-host/run_numeric > firmware/benchmarks/results/numeric.csv
/tmp/pumper-fixed-host/run_numeric quick 3 > firmware/benchmarks/results/current-h3.csv
/tmp/pumper-fixed-host/run_tail > firmware/benchmarks/results/tail.csv
```

`quick` limits the numerical matrix to three fixtures at 96 kHz. The optional
second argument sets the minimum headroom shift; the hardware runner uses three
bits. The full matrix searches shifts 0 through 30 for each test vector.

For undefined-behavior checks, configure another build with
`-DCMAKE_C_FLAGS='-O2 -g -fsanitize=undefined -fno-sanitize-recover=all'`.

## Silent device experiment

Build the benchmark into a separate directory using the normal Pico SDK setup:

```sh
cmake -S firmware -B /tmp/pumper-fixed-device -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2 \
  -DPICO_PLATFORM=rp2350-arm-s -DPICO_FLASH_SIZE_BYTES=2097152 \
  -DPUMPER_DSP_BENCHMARK=ON
cmake --build /tmp/pumper-fixed-device --target rp2350_usb_dac -j
```

The resulting `pumper_dsp_benchmark.uf2` identifies itself as **Pumper DSP
benchmark**, adds a USB serial results interface, and emits silence on I2S.
Internally it processes nonzero signals so the EQ, limiter, meters and spectrum
perform real work. Settings writes are disabled in this image.

The Linux runner requires PipeWire/WirePlumber, `pw-cat`, `pw-cli`, `pactl`,
`udisksctl`, access to the DAC's HID and serial interfaces, and an independently
saved working production UF2. It flashes the experimental image, takes live
settings snapshots, runs the tests, and restores production firmware and live
settings in a `finally` handler. Keep the backup available for manual BOOTSEL
recovery if the host process or USB connection fails.

```sh
python3 firmware/benchmarks/device_runner.py \
  --serial YOUR_DAC_SERIAL \
  --benchmark /tmp/pumper-fixed-device/pumper_dsp_benchmark.uf2 \
  --production /path/to/saved-production-5.1.uf2 \
  --output firmware/benchmarks/results/hardware.json
python3 firmware/benchmarks/summarize.py
```

`--quick` performs three short initial measurements. `--resume` retains completed
cases from the output file and runs missing cases. Run without `--resume` after
changing a kernel. The runner tests 200 MHz only for the high-precision fixed
candidate at PCM32/96 kHz when it misses the 180 MHz timing targets. After that
matrix fails, `--resume --ceiling-only` adds the authorized 250 MHz extension and
checks clock transitions. **250 MHz is the hard ceiling**, and higher clocks
apply only to PCM32/96 kHz. Other formats stay at 180 MHz; idle returns to 150 MHz.
There is no automatic voltage increase. The benchmark watchdog recovers a clock
stall to the default 150/180 MHz policy.

Results and their limitations are described in [REPORT.md](REPORT.md).

## CMSIS and M33 assembly follow-up

Add `-DPUMPER_CMSIS_BENCHMARK=ON` to both host and device configurations to
include the optimized candidates. CMake fetches the pinned CMSIS-DSP v1.16.2
archive; optionally supply `-DPUMPER_CMSIS_DSP_PATH=/path/to/CMSIS-DSP-1.16.2`
for offline builds. The production build does not fetch this dependency.

```sh
cmake -S firmware/benchmarks -B /tmp/pumper-cmsis-host \
  -DCMAKE_BUILD_TYPE=Release -DPUMPER_CMSIS_BENCHMARK=ON
cmake --build /tmp/pumper-cmsis-host -j
ctest --test-dir /tmp/pumper-cmsis-host --output-on-failure
/tmp/pumper-cmsis-host/run_cmsis_numeric > numeric.csv
/tmp/pumper-cmsis-host/run_cmsis_numeric --optimized > optimized-numeric.csv
/tmp/pumper-cmsis-host/run_cmsis_tail > tail.csv
python3 firmware/benchmarks/summarize_cmsis.py
```

Use the same device runner command with `--cmsis` for upstream CMSIS or
`--assembly` for the M33-assisted candidate. `--quick` performs one short
180 MHz check. Without it, each scenario runs for 60 seconds, progressing to
200 and then 250 MHz only when the previous clock misses the timing gates.
Production firmware and live settings are restored afterward.

See [CMSIS_REPORT.md](CMSIS_REPORT.md) for precision, performance and limitations.
