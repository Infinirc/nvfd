# NVFD — NVIDIA Fan Daemon

[繁體中文](README.zh-TW.md)

NVFD is an open-source NVIDIA GPU fan control daemon for Linux. It uses the NVML API directly, so it works on **X11, Wayland, and headless** systems — no `nvidia-settings` required.

## Features

- **Interactive TUI dashboard** — run `nvfd` to launch a real-time GPU monitoring and control interface
- **Interactive curve editor** — visual ncurses fan curve editor with mouse support
- Custom fan curves with linear interpolation and real-time temperature tracking
- Fixed fan speed mode
- True auto mode (returns control to NVIDIA driver)
- Multi-GPU support with per-GPU or all-GPU control, adaptive full/tabbed display
- Real-time temperature, utilization, memory, and power monitoring
- Systemd service with automatic fan reset on shutdown
- Config hot-reload via SIGHUP
- Auto-elevates to root (no need to type sudo)

## TUI Dashboard

```
 NVFD v1.1 ─ GPU Fan Control                                    [q] Quit
─────────────────────────────────────────────────────────────────────────
 GPU 0: NVIDIA GeForce RTX 4090
   Temp      45°C   [##############·················]
   GPU Use   78%    [########################·······]
   Memory    12.3 / 24.0 GB
   Power     285 / 450 W

   Fan 0     52%    [################···············]
   Fan 1     53%    [################···············]

   Mode:   Auto   Manual   Curve
─────────────────────────────────────────────────────────────────────────
 [Tab] GPU  [m] Mode  [M] All  [↑↓] Speed ±5  [e] Edit Curve  [q] Quit
```

> Multi-GPU: When the terminal is large enough, all GPUs are shown at once. On smaller terminals, a tab bar lets you switch between GPUs.

## Disclaimer

Use of NVFD is at your own risk. Improper fan speed settings may damage your GPU or other hardware. Infinirc is not responsible for any damage resulting from use of this software.

Recommendations:
1. Avoid setting fan speeds too low or too high.
2. Monitor GPU temperatures regularly.
3. If anything seems wrong, run `nvfd auto` to return to driver control.

## System Requirements

- NVIDIA GPU with official NVIDIA drivers (not nouveau)
- Linux operating system
- `libjansson-dev` — JSON library
- `libncursesw5-dev` — ncurses wide-character support
- NVML headers (included with CUDA toolkit or `nvidia-cuda-toolkit` package)

## Installation

### From source (recommended)

```bash
git clone https://github.com/Infinirc/nvfd.git
cd nvfd
sudo scripts/install.sh
```

The install script will:
- Detect your OS and install build dependencies
- Build the binary
- Install to `/usr/local/bin/nvfd`
- Set up the systemd service
- Migrate any existing config from v1.x

### Manual build

```bash
make
sudo make install
sudo systemctl enable --now nvfd.service
```

## Uninstallation

```bash
sudo scripts/uninstall.sh
```

Or manually:
```bash
sudo systemctl stop nvfd.service
sudo systemctl disable nvfd.service
sudo make uninstall
```

Config files in `/etc/nvfd/` are preserved. Remove manually if desired.

## Usage

```
nvfd                       Interactive TUI dashboard (on TTY)
nvfd auto                  Return fan control to NVIDIA driver
nvfd curve                 Enable custom fan curve for all GPUs
nvfd curve <temp> <speed>  Edit fan curve point (e.g., nvfd curve 60 70)
nvfd curve show            Show current fan curve
nvfd curve edit            Interactive curve editor (ncurses)
nvfd curve reset           Reset fan curve to default
nvfd <speed>               Set fixed fan speed for all GPUs (30-100)
nvfd <gpu_index> <speed>   Set fixed fan speed for specific GPU
nvfd list                  List all GPUs and their indices
nvfd status                Show current status
nvfd -h                    Show help
```

When run with no arguments on a TTY, `nvfd` launches the interactive TUI dashboard.
When started by systemd (non-TTY), it enters daemon mode automatically.

### TUI Dashboard Keys

| Key | Action |
|-----|--------|
| `Tab` / `Shift-Tab` | Switch GPU (multi-GPU) |
| `a` | Toggle sync control: single GPU ↔ all GPUs |
| `m` | Cycle mode: Auto → Manual → Curve → Auto (respects sync) |
| `M` | Cycle ALL GPUs mode (always, regardless of sync) |
| `↑` / `↓` | Adjust speed ±5% (manual mode) |
| `PgUp` / `PgDn` | Adjust speed ±10% (manual mode) |
| `e` | Open curve editor (curve mode) |
| `q` | Quit (prompts to save if settings were changed) |

### Curve Editor Keys

| Key | Action |
|-----|--------|
| `←` / `→` | Adjust temperature ±5°C |
| `↑` / `↓` | Adjust fan speed ±5% |
| `t` | Set temperature by typing a number |
| `f` | Set fan speed by typing a number |
| `a` | Add a new point |
| `d` | Delete selected point |
| `Tab` | Select next point |
| `s` | Save and quit |
| `r` | Reset to default curve |
| `q` | Quit (prompts to save if modified) |

### Modes

| Mode | Description |
|------|-------------|
| `auto` | Returns fan control to the NVIDIA driver. Fans are fully driver-managed. |
| `curve` | Controls fans using a custom temperature-to-speed curve. |
| `manual` | Fans are locked to a fixed percentage (set via `nvfd <speed>`). |

### Examples

```bash
# Launch interactive TUI dashboard
nvfd

# Set all fans to 80%
nvfd 80

# Set GPU 0 to 60%
nvfd 0 60

# Return all fans to driver control
nvfd auto

# Use custom fan curve
nvfd curve
nvfd curve show
nvfd curve 50 60    # At 50°C, run fans at 60%
nvfd curve edit     # Interactive curve editor
nvfd curve reset    # Restore default curve

# Check status
nvfd status
nvfd list
```

## Configuration

Config files are stored in `/etc/nvfd/`:

| File | Purpose |
|------|---------|
| `config.json` | Per-GPU mode settings (auto / manual / curve) |
| `curve.json` | Fan curve points (temperature → speed %) |

### Fan Curve Format

```json
{
    "30": 30,
    "40": 40,
    "50": 55,
    "60": 65,
    "70": 85,
    "80": 100
}
```

## Systemd Service

```bash
sudo systemctl start nvfd      # Start the fan control daemon
sudo systemctl stop nvfd       # Stop (fans reset to auto)
sudo systemctl restart nvfd    # Restart
sudo systemctl reload nvfd     # Reload config (SIGHUP)
sudo systemctl status nvfd     # Check status
```

The daemon resets all fans to driver-controlled auto mode on shutdown.

## Migration from v1.x

NVFD automatically migrates old configuration:

| Old | New |
|-----|-----|
| `/usr/local/bin/infinirc_gpu_fan_control` | `/usr/local/bin/nvfd` |
| `/etc/infinirc_gpu_fan_control.conf` | `/etc/nvfd/config.json` |
| `/etc/infinirc_gpu_fan_curve.json` | `/etc/nvfd/curve.json` |
| `infinirc-gpu-fan-control.service` | `nvfd.service` |

## Support

If you encounter any problems, please [open an issue](https://github.com/Infinirc/nvfd/issues).

## License

MIT License — see [LICENSE](LICENSE) for details.
