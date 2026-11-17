# bt-speaker-cli

Headless Bluetooth A2DP speaker daemon for Debian 12 (ARM64 / x86_64) that turns a Linux device into a Bluetooth audio receiver playing into PulseAudio (or PipeWire / ALSA), designed to run simultaneously alongside [sendspin-cli](https://github.com/Sendspin/sendspin-cpp-cli).

## Tested Hardware & Environment

- **Board**: Orange Pi Zero 2W (ARM64)
- **OS**: Debian 12 (Bookworm)
- **Audio stack**: PulseAudio (`pulseaudio-module-bluetooth`) + BlueZ

## Compatibility with `sendspin-cli`

`bt-speaker-cli` is designed to run concurrently with [`sendspin-cli`](https://github.com/Sendspin/sendspin-cpp-cli) on the same host without resource conflicts:

- **Shared audio output**: both run as `systemd --user` services (with `loginctl enable-linger`) and play into the same PulseAudio session (`output = pulse`). PulseAudio mixes both streams cleanly.
- **Isolated resources**: separate binary (`bt-speaker-cli`), config (`~/.config/bt-speaker-cli/config` or `/etc/bt-speaker-cli.conf`), state (`~/.local/state/bt-speaker-cli/state`), control socket (`$XDG_RUNTIME_DIR/bt-speaker-cli.sock`), and systemd units (`bt-speaker-cli.service`).
- **Stream hooks**: `--hook-start` and `--hook-stop` can optionally pause or duck `sendspin-cli` when Bluetooth playback begins.

## Features

- **BlueZ D-Bus A2DP/AVRCP management**: powers on the adapter, configures discoverability and alias, registers a pairing agent (`Just Works` by default or `--require-confirm`), and exposes AVRCP metadata and playback controls (`play`, `pause`, `stop`, `next`, `prev`).
- **Active wait & auto-reconnect**: never exits on startup if `bluetoothd`, the adapter, PulseAudio, or the remote PC is unavailable; retries with exponential backoff and reconnects automatically when the paired peer returns.
- **PulseAudio bridge**: detects `bluez_source.*` / `bluez_input.*` streams, unloads redundant `module-loopback` instances to prevent double audio, and streams PCM with linear volume scaling and 20 ms ramping into `-o pulse` (shared seamlessly with other PulseAudio clients).
- **Local control socket**: Unix socket (`$XDG_RUNTIME_DIR/bt-speaker-cli.sock`) driven by subcommands on the same binary (`status`, `devices`, `connect`, `disconnect`, `trust`, `forget`, `discoverable`, `pair`, `vol`, `mute`, `delay`, `play`, `pause`, `next`, `prev`).
- **Stream hooks**: runs `--hook-start` and `--hook-stop` asynchronously with `BT_SPEAKER_*` environment variables.

## Build (Debian 12)

```bash
sudo apt-get install -y build-essential cmake pkg-config libpulse-dev libdbus-1-dev libasound2-dev bluez pulseaudio-module-bluetooth
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Quick Start

```bash
./scripts/get_started_linux.sh
```

Or run manually from a user shell:

```bash
./build/bt-speaker-cli -n living-room-bt -o pulse
bt-speaker-cli status
bt-speaker-cli devices
```

## License

[Apache 2.0](LICENSE)
