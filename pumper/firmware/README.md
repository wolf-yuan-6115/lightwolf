# Pumper firmware

The firmware runs on the RP2354 board and exposes a composite USB Audio Class
2 device plus a vendor HID control interface. It provides a ten-band
parametric EQ, headphone crossfeed, global output processing, USB host volume
and mute, persistent profiles, and device diagnostics for the WebHID and
Android controllers.

Firmware 3.1 supports stereo PCM in these formats:

- 16-bit at 44.1, 48, 88.2, 96, 176.4, and 192 kHz.
- Packed 24-bit at 44.1, 48, 88.2, and 96 kHz.

The 24-bit alternate is not available at 176.4 or 192 kHz. Profile changes and
audio-control previews are live; persistent settings are changed only by the
corresponding save, default, or delete action.

## Requirements

- CMake 3.13 or newer
- Ninja, or another CMake build tool
- A native C/C++ compiler for Pico SDK host tools
- Arm GNU embedded toolchain, including `arm-none-eabi-gcc`
- Pico SDK 2.3.0 with its Git submodules initialized

On Debian or Ubuntu:

```sh
sudo apt install build-essential cmake git ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi
```

Check the two tools most commonly missing during configuration:

```sh
arm-none-eabi-gcc --version
ninja --version
```

## Pico SDK setup

Clone the verified SDK release with its submodules and point
`PICO_SDK_PATH` at the checkout:

```sh
git clone --branch 2.3.0 --recurse-submodules \
  https://github.com/raspberrypi/pico-sdk.git "${HOME}/Projects/pico-sdk"
export PICO_SDK_PATH="${HOME}/Projects/pico-sdk"
```

The RP2350 USB audio endpoint requires the following TinyUSB fixes when they
are not already present in the SDK checkout:

```sh
git -C "${PICO_SDK_PATH}/lib/tinyusb" fetch origin
git -C "${PICO_SDK_PATH}/lib/tinyusb" cherry-pick 86c28b76f 1ec93757f
```

Keep these SDK changes in the Pico SDK checkout; they are not project source
files.

## Configure and build

Run from the repository root. The project uses the `pico2` board definition,
the `rp2350-arm-s` platform, and the RP2354's 2 MiB flash configuration:

```sh
cmake -S firmware -B firmware/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_BOARD=pico2 \
  -DPICO_PLATFORM=rp2350-arm-s \
  -DPICO_FLASH_SIZE_BYTES=2097152

cmake --build firmware/build --target rp2350_usb_dac -j
```

The UF2 image is written to:

```text
firmware/build/rp2350_usb_dac.uf2
```

For later source changes, rerun only the build command. Configure into a fresh
build directory if the SDK, board, platform, or toolchain changes. The build
checks the firmware size and reserves the final 8 KiB of flash for settings.

## Flashing

To flash manually:

1. Disconnect the DAC.
2. Hold the RP2354 BOOTSEL button while reconnecting USB.
3. Release BOOTSEL when the USB mass-storage drive appears.
4. Copy `firmware/build/rp2350_usb_dac.uf2` to the drive.
5. Wait for the drive to disconnect and the DAC to restart.

The WebHID and Android controllers can also request a BOOTSEL restart. They do
not upload the UF2; the image must still be copied to the boot-ROM drive.
Existing profiles are stored outside the UF2 image.

## Host tests

The portable configuration, DSP, protocol, audio-format, and profile-storage
tests do not require the Pico SDK:

```sh
cmake -S firmware/tests -B /tmp/pumper-firmware-tests
cmake --build /tmp/pumper-firmware-tests
ctest --test-dir /tmp/pumper-firmware-tests --output-on-failure
```

## Troubleshooting

- `Compiler 'arm-none-eabi-gcc' not found`: install the Arm embedded toolchain
  or add it to `PATH`.
- `CMAKE_MAKE_PROGRAM is not set` with the Ninja generator: install Ninja or
  choose another installed CMake generator.
- If USB audio or its isochronous endpoint does not work, verify the TinyUSB
  fixes above, configure a fresh build directory, and flash the newly built
  UF2.
