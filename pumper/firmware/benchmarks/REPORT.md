# Fixed-point EQ feasibility results — 2026-11-12

**Decision: keep production firmware 5.1 on float DSP.** The 64-bit-history fixed-point candidate improves standalone EQ accuracy, but this portable checked implementation misses the audio deadline at 180, 200 and 250 MHz. Ordinary Q31 also has unacceptable limit-cycle behavior in the tested low-frequency filters. Neither candidate is ready to replace the working path.

The working 5.1 image, live/saved controls and the DAC-specific 32-bit/96 kHz PipeWire configuration were restored and verified. Remaining 250 MHz transition repetitions were stopped at the user’s request. No more clock tests were run.

A subsequent optimized CMSIS/assembly experiment is documented separately in
[CMSIS_REPORT.md](CMSIS_REPORT.md). The results below describe the original
checked-C experiment.

## What was tested

- Ten-band EQ only: actual production float, independent double DF1 reference, Q31 DF1 with 32-bit history, and Q31 DF1 with 64-bit history. Fixed coefficients are calculated in double and quantized with per-stage power-of-two scaling. The fixed kernels are original portable C implementations, not the optimized CMSIS library. Integer overflow/clipping checks remain enabled during hardware timing.
- Seven fixtures, all six supported rates, eight deterministic stimuli and three amplitude multipliers: **2,016 fixed-point comparisons**. Each vector has 16,384 frames; a separate impulse-tail test runs for ten seconds. Stimuli include tones, noise, impulses, steps, sweeps, multitone, extrema and adjacent codes. The CSV `dbfs` column denotes the applied amplitude multiplier; noise/adjacent-code generators have their own base amplitudes, so it is not a universal measured RMS level.
- Headroom search covers 0–30 bits. The hardware comparison uses three bits, and `current-h3.csv` repeats the relevant numerical cases with at least that headroom. All intermediate overflow and clipping invalidates a numerical case.
- Hardware processes nonzero generated samples through EQ, downstream controls, meters, spectrum and PCM packing. Every I2S block is then forcibly zeroed before DMA. It does not output test tones or music.
- Core-1 timing covers preparation/application, generation, EQ, float/fixed conversions, downstream controls, metering, packing and submission. It excludes waiting for USB input and the small result-recording operation after submission. It includes synthetic-generator and silence-guard overhead; this is deliberately more complete than the production HID DSP-load counter.
- Every 96 kHz stress case shown below ran for 60 seconds. The retained-format matrix also covers every advertised format/rate, with 60 seconds at PCM16/192 kHz and eight seconds for the other transition checks.

## Numerical findings

Current preset, 96 kHz, three-bit headroom, −6 dB stimulus scaling. Errors are RMS relative to the ideal double EQ, in dBFS (more negative is better):

| Stimulus | Production float | Q31, 32-bit history | Q31, 64-bit history |
|---|---:|---:|---:|
| 997 Hz tone | -82.7 | -109.7 | -141.3 |
| Seeded noise | -89.1 | -108.6 | -145.5 |
| Impulse | -124.9 | -73.2 | -173.3 |
| Multitone | -67.8 | -109.4 | -122.2 |

The 64-bit-history candidate improves all eight current-preset cases by at least **48.4 dB**, exceeding the 12 dB gate. Its sampled passband response deviation is about **0.0000502 dB**, below the 0.05 dB threshold. This improvement includes better coefficient calculation as well as arithmetic; it should not all be attributed to sample storage width.

At zero headroom, the ordinary Q31 impulse tail remains around **−90.7 dBFS** for the current preset at 96 kHz and **−77.3 dBFS** for the 20 Hz/Q20 fixture. The 64-bit-history outputs settle to zero at the encoded output resolution. `tail.csv` uses −600 dBFS as a display floor for exact zero; that is not an analog noise-floor claim.

Across the entire finite corpus, Q31/state32 has 1,007/1,008 cases without overflow/clipping and 983 within the response tolerance; Q31/state64 has 982/1,008 without overflow/clipping and 958 within response tolerance. Passing counts do not override the state32 limit-cycle failure. Low-level fixed-point results can be worse than float, and large headroom shifts can erase the expected precision gain.

The existing −241 dB preamp setting is below one Q31 output step even for a full-scale input. Stacked boosts can also exceed the tested fixed representation. A production design would need a defensible scaling policy and float fallback (or a different representation); this experiment does not narrow existing settings.

## Measured 32-bit / 96 kHz cost

Average and peak are complete block processing time divided by the block’s audio duration. A value above 100% misses the deadline. The proposed promotion gate was ≤70% average, ≤85% worst, and no growing underruns.

| EQ implementation | Clock | Scenario | Average | Peak | Underrun frames |
|---|---:|---|---:|---:|---:|
| Production float | 180 MHz | Steady | 68.6% | 83.2% | 0 |
| Production float | 180 MHz | Crossfeed + limiter | 73.7% | 87.1% | 0 |
| Production float | 180 MHz | Repeated ramps | 88.4% | 109.6% | 0 |
| Double DF1 reference | 180 MHz | Steady | 351.0% | 371.8% | 4,144,608 |
| Q31/state32 | 180 MHz | Steady | 210.3% | 239.2% | 3,042,048 |
| Q31/state32 | 180 MHz | Crossfeed + limiter | 215.5% | 249.3% | 3,108,768 |
| Q31/state32 | 180 MHz | Repeated ramps | 257.7% | 335.6% | 3,547,776 |
| Q31/state64 | 180 MHz | Steady | 277.4% | 306.2% | 3,708,096 |
| Q31/state64 | 180 MHz | Crossfeed + limiter | 282.6% | 316.2% | 3,745,920 |
| Q31/state64 | 180 MHz | Repeated ramps | 325.1% | 403.2% | 4,014,240 |
| Q31/state64 | 200 MHz | Steady | 249.7% | 275.5% | 3,475,296 |
| Q31/state64 | 200 MHz | Crossfeed + limiter | 254.3% | 284.6% | 3,517,536 |
| Q31/state64 | 200 MHz | Repeated ramps | 292.6% | 363.0% | 3,815,520 |
| Q31/state64 | 250 MHz | Steady | 199.8% | 225.0% | 2,895,744 |
| Q31/state64 | 250 MHz | Crossfeed + limiter | 203.6% | 227.7% | 2,950,272 |
| Q31/state64 | 250 MHz | Repeated ramps | 234.2% | 290.4% | 3,321,504 |

**250 MHz is insufficient for this kernel:** steady cost remains about twice the available budget. The clock increase behaves as expected; it does not resolve the architectural cost. The float baseline itself exceeds the deliberately conservative promotion margin under extreme continuous ramps, but retained zero underruns in these tests. This does not invalidate the much larger fixed-point overruns.

The fixed-point overflow/clipping counters remain zero in the hardware stress cases: their failures are throughput failures, not undetected numerical overflow. At 16-bit/192 kHz the wide-state candidate reaches about 564% average load, so retained-format compatibility also blocks promotion.

The 180/200 MHz matrix verified 180 MHz on other formats and 150 MHz at idle. The later 250 MHz extension verified its PCM32/96 kHz cases and return to 180 MHz at PCM32/88.2 kHz before the user stopped further repetitions. The remaining 250-extension PCM24/96 and PCM16/192 repetitions were not completed. The earlier 200-extension transition checks for those formats completed. Streaming regulator voltage remained 1.15 V; observed temperature stayed around 40°C or below.

## Resources and limitations

- Production: 91,472 byte binary and 302,124 bytes accounted SRAM. Latest benchmark: 136,972 byte binary and 357,308/532,480 bytes accounted SRAM.
- Both cores retain 2 KiB stack reservations. Static disassembly frame sizes are recorded in `metadata.json`; for example, the benchmark core-0 reporting function uses a 1,048-byte frame and the core-1 processing function a 120-byte frame, in addition to callers/callees. **Dynamic stack high-water was not measured.**
- Spectrum diagnostic high-water values reached roughly 54–61 µs, above the 50 µs acceptance ceiling. These counters are boot-scoped, not isolated per-case measurements. The experiment includes extra CDC reporting; it does not establish the production-only spectrum maximum. This remains an additional unresolved promotion gate.
- Fixed-versus-double tests are digital numerical tests, not analog DAC measurements or listening tests. Integration timing still uses float downstream processing, so it cannot claim end-to-end fixed-point precision.
- These are checked portable kernels, with per-sample dispatch, overflow checking and generic wide-state arithmetic. The result does **not** prove every Cortex-M33 fixed-point implementation is infeasible. A specialized, bounded block kernel would require new implementation and measurement.
- The first mount attempt hit an enumeration race; the runner now retries. A later HID response was lost because unsolicited reports filled the reader queue; the runner now drains that queue before requests. Working firmware was restored on both failures. Initial measurements affected by unnecessary float-baseline reference preparation were excluded from the final comparison.
- Two completed state32 cases retain the earlier image hash with the same fixed kernel. Subsequent image changes corrected float-baseline preparation and then added the 250 MHz ceiling/watchdog. Per-case hashes and build metadata preserve that provenance.

## Recommendation and delivered artifacts

Keep the current float firmware and 32-bit/96 kHz transport. Do not deploy either fixed-point candidate or enable a production overclock based on this experiment. If pursuing fixed point further, the next bounded task is to optimize the 64-bit-history EQ kernel and establish analytical headroom bounds before attempting crossfeed/gain/limiter conversion. Account for packing and configuration-update costs, rather than using the HID DSP percentage alone.

- `numeric.csv`: full numerical matrix; `current-h3.csv`: the hardware headroom comparison; `tail.csv`: post-impulse residue.
- `hardware.json`: 30 completed hardware cases and periodic samples; `summary.json`: derived gates and costs; `metadata.json`: build hashes, fixtures and resource accounting.
- `restoration.json`: verified return to production 5.1, original controls, unchanged WirePlumber rule and the original default output.
- Host reference/arithmetic/flashing validation tests, complete numerical corpus under UBSan, portable production firmware tests and both Release builds passed. Production UF2 hash remains `d4c4430e2caf0ccf2be04320eb8d67adf7283eca2cb73ab0cac46f71eda860d4`.
- No web or Android code was changed by this experiment. Unrelated localization work remains untouched.
