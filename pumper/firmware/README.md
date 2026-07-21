# Pumper firmware

The firmware enumerates as a composite USB Audio Class 2 and vendor HID device. Core 0 services TinyUSB and core 1 processes the ten-band EQ before audio enters the ping-pong PIO/DMA I2S ring. Pre-EQ input and saturated post-EQ output stereo peak/RMS metering is streamed over HID only while the controller's heartbeat remains active. Device status also reports the RP2350's approximate junction temperature, current system clock, worst DSP block time, and I2S low-water mark. The system runs at 180 MHz while an audio stream is open and returns to 150 MHz when it closes.

Ten numbered EQ profiles use the final two 4 KiB flash sectors as alternating banks. A bank is committed only after all profile pages have been programmed, so an interrupted save leaves the previous bank available. Live editing and profile loading do not erase flash. Save Profile writes a slot without changing the power-on selection; Set Default explicitly chooses which stored profile loads at boot; Delete Profile empties a slot. Deleting the default promotes the lowest-numbered remaining profile, while deleting the last profile restores the flat compiled fallback on the next boot. The first saved profile becomes the default when no profile exists. The previous single-profile record is imported into Profile 1 on first boot and migrated on the next save. The compiled fallback and Restore Defaults configuration are flat: 0 dB preamp and 0 dB filter gains.

## Prerequisites

The firmware build needs:

- CMake 3.13 or newer
- Ninja, or another CMake build tool
- A native C/C++ compiler for Pico SDK host tools such as `pioasm`
- The Arm GNU embedded toolchain, including `arm-none-eabi-gcc`
- A Pico SDK checkout with its Git submodules initialized

On Debian or Ubuntu, the core packages can be installed with:

```sh
sudo apt install build-essential cmake git ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi
```

Verify that the two tools which previously caused configuration failures are available:

```sh
arm-none-eabi-gcc --version
ninja --version
```

## Pico SDK

Clone a current Pico SDK release and initialize its submodules. This firmware has been verified with Pico SDK 2.3.0:

```sh
git clone --branch 2.3.0 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git "${HOME}/Projects/pico-sdk"
export PICO_SDK_PATH="${HOME}/Projects/pico-sdk"
```

Pico SDK 2.3.0 downloads and builds its matching `picotool` during the first configure when no compatible system installation is found. That first configure therefore needs internet access and takes longer; later builds reuse the cached tool.

The RP2350 USB audio endpoint needs the TinyUSB isochronous activation fixes discussed in Pico SDK issue 2236. Until the SDK's pinned TinyUSB revision contains them, apply the fixes to the SDK checkout itself:

```sh
git -C "${PICO_SDK_PATH}/lib/tinyusb" fetch origin
git -C "${PICO_SDK_PATH}/lib/tinyusb" cherry-pick 86c28b76f 1ec93757f
```

The second commit should be the current TinyUSB revision after applying the fix:

```sh
git -C "${PICO_SDK_PATH}/lib/tinyusb" log -2 --oneline
```

Do not add this TinyUSB patch to the Pumper repository. It fixes the SDK dependency rather than project code.

## Configure and build

Run these commands from the Pumper repository root. The hardware uses the `pico2` RP2350 board configuration, while the project overrides its flash size for the RP2354's 2 MiB internal flash.

```sh
export PICO_SDK_PATH="${HOME}/Projects/pico-sdk"

cmake -S firmware -B firmware/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_BOARD=pico2 \
  -DPICO_PLATFORM=rp2350-arm-s \
  -DPICO_FLASH_SIZE_BYTES=2097152

cmake --build firmware/build --target rp2350_usb_dac -j
```

The main output is:

```text
firmware/build/rp2350_usb_dac.uf2
```

The build also creates `.elf`, `.bin`, `.hex`, and disassembly files. A post-build check reserves the final 8 KiB of flash for the two alternating EQ profile banks and fails if the firmware grows into that area.

For normal source changes, only the build command is needed:

```sh
cmake --build firmware/build --target rp2350_usb_dac -j
```

CMake stores the SDK location, board, platform, and toolchain in its build cache. After changing any of those, configure into a new directory instead of reusing an old one:

```sh
cmake -S firmware -B firmware/build-fresh -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
  -DPICO_BOARD=pico2 \
  -DPICO_PLATFORM=rp2350-arm-s \
  -DPICO_FLASH_SIZE_BYTES=2097152

cmake --build firmware/build-fresh --target rp2350_usb_dac -j
```

No development-origin firmware flag is required. WebHID works from a secure production origin and from `http://localhost`.

## Flashing

From firmware 1.7 onward, a connected web controller can restart the DAC into
BOOTSEL mode with the Firmware update action. The browser does not receive or
upload the firmware image. Copy `firmware/build/rp2350_usb_dac.uf2` onto the
`RP2350` USB drive exposed by the boot ROM, then wait for the drive to disconnect
and the DAC to restart.

To enter BOOTSEL manually:

1. Disconnect the DAC.
2. Hold the RP2354 BOOTSEL button while reconnecting USB.
3. Release BOOTSEL after the USB mass-storage drive appears.
4. Copy `firmware/build/rp2350_usb_dac.uf2` to that drive.
5. Wait for the drive to disconnect and the DAC to restart.

The UF2 contains firmware only. Existing EQ profiles occupy the reserved final flash sectors and are not part of the UF2 image.

## Troubleshooting

`Compiler 'arm-none-eabi-gcc' not found` means the Arm embedded compiler is missing or not on `PATH`. Install the toolchain and rerun CMake in a fresh build directory.

`CMAKE_MAKE_PROGRAM is not set` with the Ninja generator means `ninja` is missing. Install Ninja or configure with another installed generator.

Errors in Pico SDK 2.1.0 host tools such as `uint8_t does not name a type` come from that older SDK and newer host compilers. Use the current SDK configuration above rather than modifying Pumper source files.

If the DAC enumerates but USB audio or its isochronous endpoint does not work, confirm that the TinyUSB log contains commits `86c28b76f` and `1ec93757f`, then configure a fresh build directory and flash the newly generated UF2.

## Host tests

The portable configuration, DSP, and HID protocol tests do not need the Pico SDK:

```sh
cmake -S firmware/tests -B /tmp/pumper-firmware-tests
cmake --build /tmp/pumper-firmware-tests
ctest --test-dir /tmp/pumper-firmware-tests --output-on-failure
```
