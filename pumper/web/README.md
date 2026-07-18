# Pumper EQ Controller

Static WebHID controller for the LightWolf Pumper USB DAC, built with React,
TypeScript, Vite, and Tailwind CSS 4. The utility-first interface uses a dark
stone palette and Lucide icons. It provides live EQ preview and a
heartbeat-controlled post-EQ stereo output meter. Ten numbered device profiles
can be loaded live and are written to flash only through the explicit Save
Profile action. A separate Make Default action selects the profile loaded at
power-on without overwriting that profile's EQ settings, and the trash action
returns a stored slot to the empty state.

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
