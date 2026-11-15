# Pinned Embassy patches

Source: crates.io `embassy-rp` 0.10.0 and `embassy-usb` 0.6.0 archives.
Licenses: upstream MIT OR Apache-2.0; copies accompany this file. The registry
metadata and upstream manifests are retained. Cargo.lock pins dependencies.
No Pico SDK or TinyUSB source is vendored here.

## embassy-usb/src/lib.rs

- `Handler::standard_descriptor` observes standard descriptor requests and may
  return static descriptor bytes. Pumper uses it for the existing macOS feedback
  variant and the exact device/BOS descriptors.
- `Handler::accept_alternate_setting` can reject a format/rate combination before
  changing interface state or touching endpoints.
- Configuration/alternate activation disables affected endpoints before enabling
  the selected alternate. An endpoint repeated in a later descriptor cannot
  disable an earlier selected alternate.
- The host-side mock-driver regression test verifies repeated addresses,
  rejection without mutation, idle alternate, and descriptor overrides.

## embassy-rp/src/usb.rs

- On RP235x, abort an endpoint transfer before resetting its DPRAM ownership;
  wait for the abort acknowledgement with a bounded fail-stop check, clear the
  pending buffer interrupt, then release abort. An abort timeout panics; the
  prototype panic handler requests ROM BOOTSEL.
- Do not arm an OUT endpoint while disabling it.
- Keep EP0 SETUP interrupts armed during data/status transfers and wake both EP0
  directions on new SETUP/reset/suspend. Cancel obsolete waits, leave events for
  the stack to consume, clear old stall state on new SETUP, and avoid applying
  an interrupted SET_ADDRESS. This implements the ControlPipe cancellation
  contract; rapid-request hardware tests are required before acceptance.
- Advance per-endpoint epochs on enable/disable and bus reset. Pending endpoint
  reads/writes return `Disabled` after a reset, including disable+enable within
  one executor poll.
- IN writes await actual transfer completion, so a HID activity pulse and the
  delayed reboot follow the acknowledgement reaching the host.

Hardware abort and DPRAM ordering still require device tests. The host mock USB
stack test does not simulate the RP2354 peripheral.

## embassy-rp/src/clocks.rs

- A narrowly scoped unsafe bookkeeping hook updates cached SYS/PERI frequencies
  after the core-1 clock owner changes the SYS divider with PIO/DMA stopped.
  PLL_SYS remains at 220 MHz; USB and timer references remain independent.
  Idle uses a 16.16 divisor of 96119 (150,000,728 Hz), ordinary streaming
  divisor 80100 (179,999,001 Hz), and PCM16/192 kHz uses divisor 65536
  (220 MHz). Keep 1.2 V in all states because fractional division contains
  220 MHz cycles. No runtime transition may select the 12 MHz reference:
  [RP2350-E12](https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008373-DS-2-rp2350-datasheet.pdf?disposition=inline)
  requires SYS at least 10% faster than USB while USB is active.

## Runtime linker

`ram.x` is adapted from cortex-m-rt 0.7.5's linker script (MIT OR
Apache-2.0). Its original comments and assertions remain, with a flash bootstrap,
RAM load addresses, and settings/stack bounds added.

The vendored RP pin-function table also has trailing whitespace removed; its
contents and behavior are unchanged.
