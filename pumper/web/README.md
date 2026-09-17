# Pumper EQ Controller

Static WebHID controller for the LightWolf Pumper USB DAC, built with React,
TypeScript, Vite, Tailwind CSS 4, and daisyUI 5. The utility-first interface
uses daisyUI's built-in light and dark themes, self-hosted Inter from Fontsource,
and Lucide icons. It provides live EQ preview and heartbeat-controlled pre- and
output stereo level meters. Ten numbered device profiles can be loaded live
and are written to flash only through the explicit Save Profile action. A
separate Make Default action selects the profile loaded at power-on without
overwriting that profile's EQ settings, and the trash action returns a stored
slot to the empty state. Device actions in the diagnostics bar can restart
firmware 1.7 or newer normally or enter BOOTSEL mode for a manual UF2 copy to
the `RP2350` USB drive; the web controller does not upload firmware images.

## USB audio and crossfeed

The USB audio group and crossfeed panel target firmware 2.2 or newer. Older
firmware keeps their new controls disabled while existing EQ controls continue
working. USB audio sits in the Global EQ pane alongside sample rate and stream
status; crossfeed follows the EQ filter table. USB master
volume and mute are read-only and set by the audio host; the downstream physical
volume slider cannot be read.

Crossfeed previews live using Off, Low, Medium, High, or Custom. Custom exposes
strength, cutoff, and delay and retains its values across preset changes. Preset
sliders show their fixed values read-only; Custom makes the parameters editable.
Save crossfeed explicitly stores its power-on setting independently of EQ
profiles. Profile operations and Restore Defaults affect EQ only.

Firmware 2.2 implements these commands. Frontend tests use mocked HID responses;
the production app contains no simulated DAC behavior.

In firmware 2.2, the red LED retains its steady streaming indication and
overlay HID activity pulses: dark while streaming and lit while idle. Controller
polling, heartbeats, and meter transfers all count as activity. Firmware 2.3
uses 25 ms pulses and at least 25 ms baseline gaps, allowing up to 20 Hz blinking.

### Firmware 2.2 HID additions

Reports retain 64-byte framing and protocol version 1. Multi-byte values are
little-endian.

| Command | Opcode | Request | Response |
| --- | --- | --- | --- |
| GetAudioControls | `0x06` | Empty | 9-byte audio controls |
| GetCrossfeed | `0x07` | Empty | 17-byte crossfeed state |
| SetCrossfeed | `0x12` | 8-byte crossfeed record | 17-byte crossfeed state |
| SaveCrossfeed | `0x26` | Empty | 17-byte crossfeed state after verified save |

Audio controls contain master/left/right signed Q8.8 dB values at offsets 0/2/4,
then master/left/right mute bytes (0 or 1) at offsets 6/7/8. Each volume is in
the range -50 to 0 dB. Effective channel volume adds master and channel dB;
effective mute combines their flags with OR. Firmware 2.3 exposes only stereo
master USB controls; left/right slots remain 0 dB and unmuted, so both effective
channels inherit master. Firmware 2.2 retains its older per-channel controls.

Crossfeed records contain mode at offset 0, a reserved zero byte at 1, strength
in basis points (`u16`, 0–4,000) at 2, cutoff Hz (`u16`, 300–2,000) at 4, and
delay microseconds (`u16`, 0–600) at 6. Modes are Off=0, Low=1, Medium=2,
High=3, Custom=4. Parameter fields retain Custom values, including in preset
modes. Default retained parameters are 20%, 700 Hz, and 0.25 ms.
Crossfeed state contains the live record at offset 0, saved record at 8, and
dirty byte (0 or 1) at 16. Preset processing uses Low: 10%/700 Hz/0.20 ms,
Medium: 20%/700 Hz/0.25 ms, High: 30%/700 Hz/0.30 ms.

New audio state is read on connection and polled once per second. Crossfeed
edits are coalesced over 55 ms and acknowledged before saving; a save may take
up to 8 seconds. Disconnect cancels queued work, and reconnect reads the device
instead of replaying browser edits.

## Development

Use a Chromium browser. WebHID is available from `http://localhost` without a special firmware build.

```sh
pnpm install
pnpm dev
```

### Linux device access

Linux normally creates generic HID nodes without write permission for the
logged-in user. Install the included rule once, reload udev, then unplug and
reconnect the DAC:

```sh
sudo install -m 0644 ../udev/70-pumper-webhid.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

The rule is restricted to the Pumper USB VID/PID and grants access only to the
active desktop session through systemd-logind.

## Verification

```sh
pnpm test
pnpm build
pnpm preview
```

## Cloudflare

The build is a static `dist/` directory. `wrangler.jsonc` configures it for Cloudflare Workers Static Assets:

```sh
pnpm deploy
```

Attach `pumper.wolf-yuan.dev` as the Worker's custom domain in the Cloudflare dashboard.
