# Pumper Controller for Android

Experimental native controller for the LightWolf Pumper USB DAC. It uses Android USB host APIs to speak the same fixed-size vendor HID protocol as the WebHID controller in `web/`.

## Current scope

- Android 14 or newer (`minSdk 34`)
- Adaptive Material 3 Expressive interface with dynamic color and expressive motion
- Live ten-band parametric EQ preview, response graph, automatic preamp, and signal meters
- Audio destination with read-only USB sample rate, stream state, and host master volume/mute
- Headphone crossfeed with live Off/Low/Medium/High/Custom preview and an independent power-on save
- Ten stored profiles with separate save, select, power-on default, and delete operations
- Device diagnostics, factory live preview, normal restart, and BOOTSEL handoff
- Automatic USB attach handling and manual connection
- Debug-only simulated DAC for UI and workflow development without hardware

Crossfeed controls require firmware 2.2 or newer. Firmware 2.3 reports the master USB volume used by the audio path; the Android app does not change host volume or mute. Crossfeed preview edits are volatile until **Save crossfeed** is pressed, and this saved state is independent of EQ profiles.

Live edits never write flash. Flash is changed only by explicit profile/crossfeed save, default-profile, or delete actions.

## Toolchain

The project is pinned to Gradle 9.4.1, Android Gradle Plugin 9.2.1, Kotlin 2.3.21, and Material 3 `1.5.0-alpha24`. It compiles against the installed Android 37.1 SDK while targeting API 37.

Use JDK 17 for command-line builds. On this workstation it is installed at:

```sh
/usr/lib/jvm/java-17-temurin
```

Android Studio can open the `android/` directory directly. If environment variables are not already configured, point `JAVA_HOME` at JDK 17 and `ANDROID_HOME` at the Android SDK before using the wrapper.

## Build and verify

Run from `android/`:

```sh
export JAVA_HOME=/usr/lib/jvm/java-17-temurin
export ANDROID_HOME=/home/wolf/Android/Sdk
./gradlew testDebugUnitTest
./gradlew lintDebug
./gradlew assembleDebug
```

The debug APK is written to `app/build/outputs/apk/debug/app-debug.apk`.

## Debug simulator

Install a debug build and choose **Open simulated DAC** on the disconnected screen. The simulator reports firmware 2.3 and runs through the real report codec, request matching, controller state, EQ and crossfeed live-preview flows, independent saving, profile commands, metering, and disconnect handling. It is not included in release builds.

The simulator does not validate Android USB permission behavior, HID endpoint discovery, or compatibility with the physical DAC. Those paths require an Android USB-host device, a data-capable USB cable or OTG adapter, and Pumper hardware.
