# Optimized wide-history EQ experiment

**Decision: keep working firmware 5.1 on float DSP.** The assembly candidate
substantially improves throughput and standalone EQ precision, but repeated
transitions still underrun at 250 MHz. No production overclock was retained.

This follow-up compares upstream CMSIS-DSP's Q31 coefficients / Q63 history
biquad with a Cortex-M33 assembly-assisted implementation. Both remain isolated
behind benchmark build flags; firmware stays at 5.1. The original checked-C
results remain in [REPORT.md](REPORT.md).

## Kernels and validation

The upstream source is [CMSIS-DSP v1.16.2's 32x64 DF1 kernel](https://github.com/ARM-software/CMSIS-DSP/blob/v1.16.2/Source/FilteringFunctions/arm_biquad_cascade_df1_32x64_q31.c),
compiled with loop unrolling and `-O3`. It is downloaded into the build tree or
read from an explicitly supplied external checkout, never vendored into the SDK.
The archive is pinned by SHA-256 in `cmsis_dependency.cmake`.

Mode 4 uses the upstream block kernel for steady processing and a sample-wise
coefficient ramp. Mode 5 uses an inline M33 SMULL/SMLAL sequence for the
64-by-32 feedback products and processes transitions one stage at a time across
a block. That preserves each stage's per-sample 10 ms coefficient trajectory
while reducing repeated dispatch and state access. Both use the same per-stage
coefficient scaling and three input headroom bits on the device.

An independent checked 128-bit oracle audits intermediate accumulator and
history ranges. Host tests compare output words and full histories across all
six rates, variable packet sizes, interrupted ramps and silence. The optimized
and upstream numerical matrices are identical across 1,008 test vectors under
UBSan. A boot-time device self-test compares both kernels, including interrupted
ramps, before allowing mode 5 to run. Linked disassembly confirms the ARM
multiply-accumulate instructions are present.

These kernels intentionally omit hot-loop overflow checks. The oracle certifies
the tested finite stimuli, not arbitrary user settings or inputs. Hardware
`arithmetic_checks:false` explicitly identifies this difference from the earlier
checked-C experiment; zero overflow counters are not evidence of safety.

## Precision

For the current preset at 96 kHz, with three headroom bits and the −6 dB stimulus
multiplier, CMSIS improves RMS error relative to the double reference by
27.2–96.3 dB over production float across the eight stimuli. This includes
improved coefficient calculation, not just wider history.

CMSIS truncates, while the previous checked kernel rounds. Its current-preset
post-impulse residue is about −149.5 dBFS at both 96 and 192 kHz; the 20 Hz/Q20
fixture is around −169 to −171 dBFS RMS. It is not exact zero. See
`results/cmsis/numeric.csv` and `tail.csv` for the complete finite corpus.

Across the full corpus, 966/1,008 vectors find a range-safe headroom shift;
942 also meet the sampled 0.05 dB response threshold. The search can consume
up to 30 bits, and some settings cannot be represented safely at all. These
failures prevent treating the current-preset result as general acceptance.

Three headroom bits reduce input resolution, and each stage still emits Q31.
Downstream crossfeed, host gain, output processing and limiter remain float in
this experiment. It therefore does not establish end-to-end 32-bit precision.

## Packing optimization

The original PCM32 packer converts float to double for scaling and rounding.
The benchmark instead derives the exact float value from its IEEE binary32
bits and evaluates the same ×49152 scaling with integer arithmetic. It retains
channel compensation, round-away-from-zero behavior and the fixed 0.75 safety
ceiling, including saturation, NaN and infinity handling. Two million random
bit patterns, rounding-boundary cases and overlapping buffers compare
bit-for-bit against the production packer. This optimization is benchmark-only.

## Hardware method

Each case generates nonzero signals internally and exercises all ten bands,
controls, meters, spectrum and PCM packing. Every submitted I2S block is then
zeroed before DMA: no test audio is emitted. Timing includes generation,
conversions, configuration updates, packing and submission, not just EQ.
Scenario 0 is steady, scenario 1 adds crossfeed and limiter activity, and
scenario 2 repeatedly ramps EQ and controls. The promotion thresholds remain
≤70% average block budget, ≤85% peak and no underruns.

Only PCM32/96 kHz can use 200 or 250 MHz; 250 MHz is the hard ceiling.
Streaming voltage remains 1.15 V. Idle returns to 150 MHz and other streams
retain 180 MHz. A watchdog provides recovery from a stalled benchmark.
Short successful runs establish observed behavior on this unit, not general
long-term overclock stability.

## Measured complete-block cost

Every row is a 60-second PCM32/96 kHz run. Both candidates use the same fast
packer, so their comparison isolates the EQ/transition changes. The earlier
checked-C timing includes the slower production packer and runtime overflow
checks; the overall speedup cannot all be attributed to assembly.

| Kernel | MHz | Scenario | Average | Peak | Underrun frames |
|---|---:|---|---:|---:|---:|
| CMSIS | 180 | Steady | 96.8% | 133.4% | 0 |
| CMSIS | 180 | Crossfeed + limiter | 102.4% | 137.1% | 141,312 |
| CMSIS | 180 | Repeated ramps | 141.9% | 208.5% | 1,713,504 |
| CMSIS | 200 | Steady | 87.3% | 114.3% | 0 |
| CMSIS | 200 | Crossfeed + limiter | 92.0% | 123.4% | 0 |
| CMSIS | 200 | Repeated ramps | 127.7% | 187.6% | 1,259,712 |
| CMSIS | 250 | Steady | 69.9% | 91.5% | 0 |
| CMSIS | 250 | Crossfeed + limiter | 73.8% | 98.7% | 0 |
| CMSIS | 250 | Repeated ramps | 102.2% | 150.3% | 275,904 |
| M33 assembly | 180 | Steady | 83.9% | 119.6% | 0 |
| M33 assembly | 180 | Crossfeed + limiter | 89.2% | 123.1% | 0 |
| M33 assembly | 180 | Repeated ramps | 123.8% | 185.8% | 1,119,936 |
| M33 assembly | 200 | Steady | 75.6% | 101.9% | 0 |
| M33 assembly | 200 | Crossfeed + limiter | 80.4% | 111.1% | 0 |
| M33 assembly | 200 | Repeated ramps | 111.4% | 167.3% | 611,040 |
| M33 assembly | 250 | Steady | 60.5% | 81.6% | 0 |
| M33 assembly | 250 | Crossfeed + limiter | 64.4% | 88.7% | 0 |
| M33 assembly | 250 | Repeated ramps | 89.2% | 134.3% | 6,816 |

The assembly candidate passes the steady timing gate at 250 MHz. Crossfeed and
limiter retain zero underruns but miss the peak margin. Repeated ramps still
produce 6,816 underrun frames in 60 seconds and peak above the block deadline.
This is far closer than the checked-C kernel, but it does not fix the complete
96 kHz playback problem. The remaining work includes transition/preparation
cost, a defensible overflow/headroom policy and precision in downstream DSP.

The device stayed responsive through the tested 180/200/250 MHz cases and
returned to 150 MHz when idle. Maximum sampled temperature was approximately
40.8°C. No claim of long-term 250 MHz stability is made. Other-format hardware
timing, including PCM16/192 kHz, was not repeated for these new kernels; it
remains required before any production adoption.

Spectrum's boot-scoped worst slice reached 60 µs, above its 50 µs acceptance
ceiling. It includes the benchmark's additional CDC activity and is not an
isolated production measurement. This remains another unresolved gate.

Both full experiments restored the saved production image and live controls.
The final readback confirms firmware 5.1, idle at 150 MHz, matching global/band,
profile, crossfeed and output settings, and the original default PipeWire sink
configured for S32LE stereo at 96 kHz. The DAC-specific WirePlumber rule is
unchanged. No test audio was emitted. See `results/cmsis/restoration.json`.

## Build and verification

- Release target: `pico2`, `rp2350-arm-s`, 2 MiB flash. The assembly benchmark
  occupies 146,428 bytes of binary and 381,472/532,480 bytes of accounted SRAM.
  The final 8 KiB settings reservation passes the build check. Dynamic stack
  high-water is unmeasured.
- Five optimized host tests pass normally and under UBSan, including exact
  packing and kernel/state comparisons. Three default benchmark tests and all
  ten portable production firmware tests pass.
- The complete 1,008-vector optimized host matrix is byte-identical to the
  upstream matrix under UBSan. The upstream source uses its documented
  two's-complement shift/wrap assumptions; its shift sanitizer is disabled,
  while the independent oracle checks intermediate ranges.
- Production firmware is byte-identical to the saved working 5.1 UF2:
  `d4c4430e2caf0ccf2be04320eb8d67adf7283eca2cb73ab0cac46f71eda860d4`.
- Web and Android checks were not rerun: this experiment changes no controller
  code. Unrelated localization edits are preserved.
- Source/dependency hashes and build accounting are in
  `results/cmsis/metadata.json`. Recompute derived hardware/numerical results
  with `python3 firmware/benchmarks/summarize_cmsis.py`.
