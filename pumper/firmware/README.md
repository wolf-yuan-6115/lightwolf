# Pumper firmware

The firmware enumerates as a composite USB Audio Class 2 and vendor HID device. Core 0 services TinyUSB while core 1 runs the floating-point audio chain and hands completed blocks directly to ping-pong PIO/DMA. Pre-EQ input and final-output stereo peak/RMS metering is streamed over HID only while the controller's heartbeat remains active. Device status also reports the RP2350's approximate junction temperature, current system clock, worst DSP block time, and I2S low-water mark. The system runs at 180 MHz while an audio stream is open and returns to 150 MHz when it closes.

Ten numbered EQ profiles use the final two 4 KiB flash sectors as alternating banks. A bank is committed only after all profile pages have been programmed, so an interrupted save leaves the previous bank available. Live editing and profile loading do not erase flash. Save Profile writes a slot without changing the power-on selection; Set Default explicitly chooses which stored profile loads at boot; Delete Profile empties a slot. Deleting the default promotes the lowest-numbered remaining profile, while deleting the last profile restores the flat compiled fallback on the next boot. The first saved profile becomes the default when no profile exists. The previous single-profile record is imported into Profile 1 on first boot and migrated on the next save. The compiled fallback and Restore Defaults configuration are flat: 0 dB preamp and 0 dB filter gains.

## Firmware 3.0 audio path and controls

The UAC2 streaming interface offers 16-bit stereo PCM at 44.1, 48, 88.2, 96,
176.4, and 192 kHz, plus packed 24-bit stereo PCM at 44.1, 48, 88.2, and 96 kHz.
The 24-bit alternate cannot be selected at 176.4 or 192 kHz. I2S uses two 16-bit
slots for 16-bit input and two 32-bit slots with left-aligned 24-bit samples for
24-bit input.

Six aligned blocks move by ownership from USB decoding on core 0 to DSP on core 1
and then directly to ping-pong DMA. Output primes about 2 ms before playback; an
underrun emits silence and returns to priming. The TinyUSB receive buffer is 4 KiB.
Configuration is published through immutable snapshots, and the HID meter swaps
accumulators without holding a spinlock in the audio path.

Processing order is EQ, crossfeed, USB volume/mute, global output processing,
limiter, then final PCM quantization. The always-on stereo-linked sample-peak
limiter has immediate attack, a -1 dBFS soft knee, and a 50 ms release. It has no
lookahead, true-peak oversampling, dither, or user toggle. Output processing adds
swap, independent polarity inversion, mono, 0–200% stereo width, and -100% to
+100% balance. Changes ramp over 10 ms and remain global rather than profile-bound.

The ten EQ bands additionally accept RBJ low-pass, high-pass, notch, and
constant-0-dB-peak band-pass filters. These types use frequency and Q; gain is
ignored and bandwidth mode is rejected.

Storage schema 4 adds a global output-processing page to each alternating bank.
Older banks import neutral output settings without writing flash and migrate only
on an explicit save. Data pages are programmed before the commit header, so CRC
validation can fall back to the previous complete bank after an interrupted write.

The red LED remains on while streaming and fully off while idle. While streaming,
received HID output reports (including malformed or rejected reports) and completed
HID input transfers trigger an inverted 25 ms pulse, followed by at least 25 ms at baseline.
Additional traffic coalesces into one pending pulse, so continuous polling and
meter traffic visibly blink at up to 20 Hz. Audio packets and UAC2 controls do
not count. The non-blocking core-0 LED task owns runtime red output, preserves
active-low PWM and its brightness cap, tolerates timer wraparound, and resets
activity on USB unmount.

HID framing stays at 64 bytes with protocol version 1. Firmware 3.0 adds
GetOutputProcessing (`0x08`), SetOutputProcessing (`0x13`), and
SaveOutputProcessing (`0x27`). Their 8-byte config contains flags, signed balance
basis points, width basis points, and zeroed reserved fields. Responses include
live config, saved config, and dirty state.

### Board verification

After flashing, verify every advertised host format, both I2S slot layouts, rate
switching, output controls, persistence, and invalid 24-bit high-rate rejection.
Sustain the maximum 16-bit and 24-bit modes with all DSP active and confirm there
are no post-startup underruns. Portable tests cannot establish board DSP margin or
replace USB, logic-analyzer, and listening checks.

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

The build also creates `.elf`, `.bin`, `.hex`, and disassembly files. A post-build check reserves the final 8 KiB of flash for the two alternating EQ profile banks and fails if the firmware grows into that area. The following console-only resource report summarizes the ELF flash/SRAM sections, configured stacks and heap, peripheral use, remaining capacity, and warns at 80% utilization. CPU utilization still requires measurement on hardware.

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
