# Pumper Agent Guide

## Project overview

Pumper is an RP2354-based USB Audio Class 2 DAC with a ten-band parametric EQ. The firmware also exposes a vendor HID interface used by the React WebHID controller in `web/`.

Read `firmware/README.md` for toolchain, Pico SDK, TinyUSB, flashing, and troubleshooting details. Read `web/README.md` for frontend development and WebHID setup.

## Repository layout

- `firmware/src/`: RP2354 firmware, USB descriptors, DSP, I2S, HID protocol, and profile storage.
- `firmware/tests/`: portable host tests for EQ, protocol, and simulated flash storage.
- `web/src/`: React/TypeScript WebHID controller.

Do not edit generated directories such as `firmware/build*`, `web/dist`, or `web/node_modules`.
Preserve unrelated uncommitted changes; do not reset or rewrite user work.

## Firmware rules

- Target `PICO_BOARD=pico2`, `PICO_PLATFORM=rp2350-arm-s`, and 2 MiB flash for the RP2354.
- Core 0 services TinyUSB/WebHID. Core 1 performs EQ and writes audio to I2S. Do not block the USB task or allocate in the audio path.
- The browser meter receives the saturated post-EQ samples immediately before I2S output.
- `LED_PWM_ON_LEVEL` is the maximum brightness for both the fixed LED state and blue audio meter.
- The final 8 KiB of flash is reserved for two alternating profile banks. Preserve the post-build size check and power-loss fallback.
- Profile save, profile selection, and power-on default selection are separate operations. Flash writes happen only for explicit save/default/delete actions.
- HID reports are fixed at 64 bytes. Protocol changes must be made on both sides: `firmware/src/eq_protocol.h`, firmware command handling, and `web/src/protocol.ts`. Bump the firmware minor version when adding commands or changing payload semantics.
- Keep Pico SDK/TinyUSB fixes outside this repository. The currently verified TinyUSB fixes are commits `86c28b76f` and `1ec93757f` in the SDK submodule.

## Frontend rules

- Use React, TypeScript, Tailwind CSS 4, and Lucide icons.
- Keep the UI utility-first with dark `stone` surfaces. `web/src/styles.css` should contain only Tailwind import and genuinely global browser defaults.
- Use Lucide icons for command buttons; do not add hand-drawn icon SVGs. The EQ graph remains a data visualization and may use SVG directly.
- Preserve live preview behavior: control edits update the DAC without writing flash.
- An empty profile is a save target, not an unsaved EQ edit. Only actual EQ edits should trigger the discard confirmation.
- WebHID works on secure origins and `http://localhost`; no firmware development-origin flag is required.
- Do not add or run Playwright unless explicitly requested. Use Vitest, the production build, and manual browser verification.

## Build and verify

Run firmware commands from the repository root:

```sh
export PICO_SDK_PATH="Path to Pico SDK"
cmake -S firmware -B firmware/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_BOARD=pico2 \
  -DPICO_PLATFORM=rp2350-arm-s \
  -DPICO_FLASH_SIZE_BYTES=2097152
cmake --build firmware/build --target rp2350_usb_dac -j
```

Run portable firmware tests after DSP, protocol, or storage changes:

```sh
cmake -S firmware/tests -B /tmp/pumper-firmware-tests
cmake --build /tmp/pumper-firmware-tests -j
ctest --test-dir /tmp/pumper-firmware-tests --output-on-failure
```

Run frontend verification from `web/`:

```sh
pnpm test
pnpm build
```

Before handing off, run `git diff --check` and report any verification that could not be performed.
