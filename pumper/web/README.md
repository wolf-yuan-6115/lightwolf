# Pumper WebHID controller

The web controller is a static React application for the LightWolf Pumper USB
DAC. It uses TypeScript, Vite, Tailwind CSS 4, daisyUI, and Lucide icons.

It provides:

- Live ten-band parametric EQ preview with response graph, automatic preamp,
  and input/output level meters.
- Ten device profiles with separate load, save, default, and delete actions.
- USB sample-rate, bit-depth, stream, volume, and mute status.
- Headphone crossfeed and, on firmware 3.0 or newer, global output processing.
- The additional low-pass, high-pass, notch, and constant-peak band-pass EQ
  filters available on firmware 3.0 or newer.
- Firmware restart and BOOTSEL handoff actions.

Live edits are sent to the DAC for preview. Flash is changed only by an
explicit profile, crossfeed, output-processing, default, or delete action.

## Requirements

- Chromium-based browser with WebHID support
- Node.js and pnpm
- A Pumper DAC for hardware control

WebHID works from a secure origin and from `http://localhost`; no special
firmware development-origin flag is required.

## Development

From this directory:

```sh
pnpm install
pnpm dev
```

Open the local Vite URL in Chromium and connect the DAC over USB.

### Linux device access

Linux may create generic HID nodes without write permission for the logged-in
user. Install the project rule once, reload udev, and reconnect the DAC:

```sh
sudo install -m 0644 ../udev/70-pumper-webhid.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

The rule is restricted to the Pumper USB VID/PID and grants access to the
active desktop session through systemd-logind.

## Verification

```sh
pnpm test
pnpm build
pnpm preview
```

The production build is written to `dist/`.

## Deployment

The project is configured for Cloudflare Workers Static Assets:

```sh
pnpm deploy
```

Configure the Worker's custom domain separately in Cloudflare if deploying to
`pumper.wolf-yuan.dev`.
