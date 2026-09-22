# Pumper Controller for Android

The Android app is an experimental native controller for the LightWolf Pumper
USB DAC. It uses Android USB host APIs and the same HID control protocol as the
WebHID controller in [`../web/`](../web/).

The app provides:

- Live ten-band parametric EQ preview, response graph, automatic preamp,
  signal meters, and a firmware 3.3 limiter activity indicator.
- Audio status including USB sample rate, bit depth, stream state, and host
  master volume/mute.
- Headphone crossfeed with Off, Low, Medium, High, and Custom modes.
- Firmware 3.0 output processing for mono, channel swap, polarity, balance,
  and stereo width.
- Firmware 3.0 low-pass, high-pass, notch, and constant-peak band-pass EQ
  filters.
- Ten profiles with separate save, select, default, and delete actions.
- Firmware diagnostics, normal restart, and BOOTSEL handoff.

Crossfeed requires firmware 2.2 or newer. Output processing and the additional
EQ filter types require firmware 3.0 or newer. Live edits do not write flash;
the app saves crossfeed, output processing, and profiles independently through
explicit actions.

## Requirements

- Android 14 or newer (`minSdk 34`)
- Android SDK API 37
- JDK 17
- Android Studio, or the included Gradle wrapper
- An Android USB-host device and a data-capable USB cable or OTG adapter for
  physical DAC testing

If the SDK or Java paths are not configured, set `ANDROID_HOME` and
`JAVA_HOME` before using the Gradle wrapper.

## Build and verify

Run from this directory:

```sh
./gradlew testDebugUnitTest
./gradlew lintDebug
./gradlew assembleDebug
```

The debug APK is written to
`app/build/outputs/apk/debug/app-debug.apk`. To install it on a connected
development device, run:

```sh
./gradlew installDebug
```

Android Studio can open the `android/` directory directly.

## Debug simulator

Debug builds include **Open simulated DAC** on the disconnected screen. The
simulator allows UI and controller workflows to be exercised without Pumper
hardware. It is not included in release builds.

The simulator does not validate Android USB permissions, HID endpoint
discovery, or compatibility with a physical DAC. Those paths require an
Android USB-host device and Pumper hardware.
