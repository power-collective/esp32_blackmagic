# Black Magic Probe for ESP32

An ESP32 port of the Black Magic Probe firmware, providing a WiFi-based or serial ARM Cortex-M debug interface via SWD/JTAG.

Based on the [ESP8266 Black Magic port](https://github.com/markrages/blackmagic/tree/a1d5386ce43189f0ac23300bea9b4d9f26869ffb/src/platforms/esp8266) and uses upstream [Black Magic Probe](https://github.com/blackmagic-debug/blackmagic) code via git submodule.

## Features

### Core Debugging
- **SWD debugging** for ARM Cortex-M processors
- **JTAG debugging** for RISC-V targets (ESP32-C3/C6)
- Support for **50+ target microcontroller families** via upstream BMP
- **RTT (Real-Time Transfer)** support for fast bidirectional communication
- GDB Remote Serial Protocol over WiFi or USB serial

### Connectivity Modes

#### WiFi Mode
- **Access Point mode** (default): ESP32 hosts its own WiFi network
  - SSID: `blackmagic`
  - Password: `blackmagic`
  - IP: `192.168.4.1`
  - GDB Port: `2345`
- **Station mode**: Connect to existing WiFi network
- **Web UI**: Real-time monitoring at `http://192.168.4.1`
  - WebSocket support for live UART/RTT/SWO output
  - System status and GDB connection info
  - Target detection and monitoring

#### Serial Mode
- **GDB over UART0** (USB serial connection)
- No network infrastructure required
- Lower latency than WiFi (~1-2ms vs ~10-20ms)
- Standard 115200 baud (configurable)
- Direct USB connection for simplicity

#### Runtime Mode Switching
Select WiFi or Serial mode at boot via GPIO39 button:
1. Power on device
2. Within 5 seconds:
   - Button pressed (GPIO39 LOW) → WiFi mode
   - Button not pressed → Serial mode (default)
3. Connect to the selected interface

Mode selection occurs at boot time without recompilation.

## Supported Hardware

**Tested platforms:**
- **M5Stack Atom Lite** (ESP32) - Default configuration
- ESP32-C3 (RISC-V)
- ESP32-C6 (RISC-V) - Seeed XIAO ESP32-C6
- ESP32-S3 (Xtensa)

**Target support:**
- All targets supported by upstream Black Magic Probe:
  - STM32 (F0, F1, F2, F3, F4, F7, G0, H5, H7, L0, L1, L4, L5, etc.)
  - Nordic nRF51, nRF52, nRF91
  - NXP LPC, Kinetis, i.MX RT
  - Atmel/Microchip SAM, SAMD, SAME
  - **Puya PY32F series** (PY32F002A, PY32F07x, etc.)
  - Raspberry Pi RP2040, RP2350
  - WCH CH32F
  - And many more...
- ESP32-C3/C6 (via JTAG)

**Known limitations:**
- ESP32-C3 devkitc-02 with chip revision v0.2 is not supported due to hardware bugs

## Quick Start

### 1. Building

#### PlatformIO (Recommended)

```bash
# Clone repository
git clone --recursive https://github.com/[your-repo]/esp32_blackmagic.git
cd esp32_blackmagic

# Build and upload
pio run -e m5stack-atom -t upload
```

Available environments in `platformio.ini`:
- `m5stack-atom`: M5Stack Atom Lite (ESP32)

For other boards, create additional environments or modify pin configurations in `main/platform.h`.

#### ESP-IDF

```bash
. ~/esp/esp-idf/export.sh
cd esp32_blackmagic

# Review pin configuration in main/platform.h
# Configure WiFi credentials via menuconfig
idf.py build
idf.py flash
```

### 2. First Boot

**Default behavior (Serial mode):**
```
[Boot messages...]
I (610) blackmagic: Checking mode select button (GPIO39)...
I (610) blackmagic: Press and hold button for WiFi mode, release for Serial mode
[5 second wait...]
I (5620) blackmagic: No button press - Serial mode selected (default)
I (5620) blackmagic: === GDB SERIAL MODE ===
I (5620) blackmagic: UART0 @ 115200 baud
I (5630) blackmagic: GDB serial ready!
I (5630) blackmagic: Connect with: arm-none-eabi-gdb -ex 'target remote /dev/ttyUSB0'
```

**With button held (WiFi mode):**
```
I (2920) blackmagic: Button detected! WiFi mode selected
I (2920) blackmagic: === GDB WIFI MODE ===
I (2920) blackmagic: Access Point mode - creating WiFi network
[WiFi startup messages...]
I (5430) blackmagic: wifi_init_softap finished. SSID:blackmagic password:blackmagic channel:1
I (5440) blackmagic: Web server started - informational view at http://192.168.4.1
```

## Usage

### Serial Mode (Default)

1. Connect ESP32 to computer via USB
2. Power on (don't press button)
3. Connect with GDB:

```bash
arm-none-eabi-gdb firmware.elf
(gdb) target extended-remote /dev/ttyUSB0
(gdb) monitor swdp_scan
Target voltage: not supported on this platform.
Available Targets:
No. Att Driver
 1      STM32F4xx M4
(gdb) attach 1
(gdb) load
(gdb) continue
```

**Linux/macOS port names:**
- `/dev/ttyUSB0` (FTDI, CH340)
- `/dev/ttyACM0` (native USB-CDC)
- `/dev/cu.usbserial-*` (macOS)

**Windows:**
- `COM3`, `COM4`, etc.

### WiFi Mode

1. Connect ESP32 to power
2. **Hold GPIO39 button during boot** (5 seconds)
3. Connect to WiFi network `blackmagic` (password: `blackmagic`)
4. Connect with GDB:

```bash
arm-none-eabi-gdb firmware.elf
(gdb) target extended-remote 192.168.4.1:2345
(gdb) monitor swdp_scan
(gdb) attach 1
(gdb) load
(gdb) continue
```

**Web UI:** Open browser to `http://192.168.4.1` for:
- GDB connection status
- Target information
- Live UART/RTT/SWO output terminal
- System statistics

### PlatformIO Integration

Add to your `platformio.ini`:

```ini
[env:your_target]
platform = ststm32
board = blackpill_f411ce
framework = stm32cube

# For WiFi mode:
debug_tool = blackmagic
debug_port = 192.168.4.1:2345

# For Serial mode:
debug_tool = blackmagic
debug_port = /dev/ttyUSB0
```

Then use: `pio debug`

### Monitor Commands

**Standard Black Magic Probe commands:**
```
monitor help                    # List all available commands
monitor swdp_scan              # Scan for SWD targets
monitor jtag_scan              # Scan for JTAG targets
monitor version                # Show firmware version
monitor reset                  # Reset target
monitor tpwr enable            # Enable target power (if supported)
```

**RTT support:**
```
monitor rtt enable             # Enable RTT
monitor rtt channel 0          # Select RTT channel
monitor rtt poll <time>        # Set polling interval
```

**SWO trace (if enabled):**
```
monitor traceswo 115200        # Configure SWO baud rate
```

## Pin Configuration

### M5Stack Atom Lite (Current Default)

**SWD Interface:**
| Signal | GPIO | Atom Lite Header |
|--------|------|------------------|
| SWDIO  | 21   | G21 (Row 1)      |
| SWCLK  | 25   | G25 (Row 1)      |
| NRST   | 23   | G23 (Row 2)      |

**JTAG Interface:**
| Signal | GPIO | Atom Lite Header |
|--------|------|------------------|
| TMS    | 21   | G21 (Row 1)      |
| TCK    | 25   | G25 (Row 1)      |
| TDI    | 22   | G22 (Row 2)      |
| TDO    | 19   | G19 (Row 2)      |

**Control:**
| Function | GPIO | Atom Lite Header |
|----------|------|------------------|
| Mode Button | 39 | Built-in button |

**Optional (disabled by default):**
| Function | GPIO | Notes |
|----------|------|-------|
| TRACESWO | 33   | Set `PLATFORM_HAS_TRACESWO=1` |
| Debug UART TX | 26 | Set `USE_CUSTOM_DEBUG_UART=1` |
| Debug UART RX | 32 | Set `USE_CUSTOM_DEBUG_UART=1` |

### Hardware Connections Example

```
Target MCU          ESP32 (Atom Lite)
┌─────────┐         ┌──────────┐
│         │         │          │
│ GND ────┼─────────┼─ GND     │
│ SWDIO ──┼─────────┼─ G21     │
│ SWCLK ──┼─────────┼─ G25     │
│ 3V3 ────┼─ ─ ─ ─ ┼─ 3V3     │ (optional - target power)
│         │         │          │
└─────────┘         └──────────┘
```

**Important:** Ensure common ground between ESP32 and target!

## Configuration

### Changing Pins

Edit `main/platform.h`:

```c
// M5Stack Atom Lite pin mapping for JTAG/SWD:
#define SWDIO_PIN  (21)  // GPIO21 (M5Stack Atom Lite)
#define SWCLK_PIN  (25)  // GPIO25 (M5Stack Atom Lite)
#define NRST_PIN   (23)  // GPIO23 (M5Stack Atom Lite)

// For other boards, change these GPIO numbers
```

### WiFi Credentials

**Method 1: Edit Kconfig defaults**

Edit `main/Kconfig.projbuild`:
```
config WIFI_SSID
    string "WiFi SSID"
    default "your_ssid"

config WIFI_PASSWORD
    string "WiFi Password"
    default "your_password"
```

**Method 2: menuconfig**
```bash
pio run -t menuconfig
# Navigate to: Wi-Fi Configuration
```

**Method 3: Edit sdkconfig directly**

Edit `sdkconfig.m5stack-atom`:
```
CONFIG_WIFI_SSID="your_ssid"
CONFIG_WIFI_PASSWORD="your_password"
```

### Switching WiFi AP/Station Mode

Edit `main/main.c`:

```c
// Line 85:
#define AP_MODE 1  // 1 = Access Point, 0 = Station mode
```

For Station mode, configure your router's SSID/password via menuconfig or sdkconfig.

### Debug Log Configuration

By default, ESP32 logs go to UART0 (same as GDB in serial mode). To separate logs:

Edit `main/platform.h`:
```c
#define USE_CUSTOM_DEBUG_UART 1  // Redirect logs to UART1 (GPIO26/32)
```

This is **recommended when using Serial GDB mode** to prevent log interference.

## Advanced Features

### Enabling/Disabling Features

Edit `main/platform.h`:

```c
// Runtime mode switching (enabled by default)
#define MODE_SELECT_BUTTON_PIN 39
#define MODE_SELECT_TIMEOUT_MS 5000

// Debug UART (disabled by default)
#define USE_CUSTOM_DEBUG_UART 0  // 1 to enable on GPIO26/32

// SWO Trace (disabled by default)
#define PLATFORM_HAS_TRACESWO 0  // 1 to enable on GPIO33

// UART Passthrough (disabled by default)
#define PLATFORM_HAS_UART_PASSTHROUGH 0  // 1 to enable
```

### Target-Specific Support

#### Puya PY32F Series
Automatically detected via SWD. Supports:
- PY32F002A (tested)
- PY32F003
- PY32F030
- PY32F07x series

No special configuration needed.

#### ESP32-C3/C6 JTAG
To debug ESP32-C3/C6 targets via JTAG, use `monitor jtag_scan` instead of `swdp_scan`.

### RTT Usage

```bash
# In GDB session:
(gdb) monitor rtt enable
(gdb) monitor rtt channel 0
(gdb) continue

# RTT output will appear in terminal
# For WiFi mode, also visible in web UI
```

### Web UI Features

Access `http://192.168.4.1` when in WiFi mode:

- **Real-time terminal**: View UART, RTT, and SWO output via WebSocket
- **Connection status**: GDB client connection state
- **Target info**: Attached target name and details
- **System stats**: Free heap, IP address, GDB port

## Troubleshooting

### Build Issues

**Flash size mismatch warning:**
```
Warning! Flash memory size mismatch detected. Expected 4MB, found 2MB!
```
- Update `sdkconfig.defaults`: `CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y`
- Or ignore if you have 4MB flash

**Watchdog timeout:**
```bash
pio run -t menuconfig
# Component config → ESP System Settings → Task Watchdog
# Adjust or disable as needed
```

### Runtime Issues

**Serial mode: No response from GDB**
- Check baud rate (115200)
- Verify correct serial port
- Try resetting ESP32
- Check if logs interfere (enable `USE_CUSTOM_DEBUG_UART`)

**WiFi mode: Can't connect to AP**
- Verify password is at least 8 characters
- Check if AP mode is enabled (`AP_MODE 1`)
- Look for "wifi_init_softap finished" in logs
- Try different WiFi channel (edit `main.c` line 248)

**Button not working:**
- Verify GPIO39 is the correct button pin for your board
- Check pull-up is enabled (should be by default)
- Button must be active-low (pressed = GND)

**Target not detected:**
```
(gdb) monitor swdp_scan
Target voltage: not supported on this platform.
Available Targets:
No. Att Driver
```
- Check wiring (especially GND!)
- Verify target has power
- Try slower SWD clock: Add delay in `platform.h`
- Check target is not in low-power mode

### Getting Help

Check logs via serial console:
```bash
# Linux/macOS
screen /dev/ttyUSB0 115200

# Or use PlatformIO monitor
pio device monitor -b 115200
```

## Development

### Project Structure

```
esp32_blackmagic/
├── main/
│   ├── main.c              # Entry point, WiFi, mode selection
│   ├── platform.h          # Pin definitions, feature flags
│   ├── platform.c          # ESP32 platform initialization
│   ├── gdb_if.c            # TCP/WiFi GDB interface
│   ├── gdb_if_serial.c     # UART/Serial GDB interface
│   ├── gdb_if_wrapper.c    # Runtime mode dispatcher
│   ├── web_server.c        # HTTP/WebSocket server
│   └── ...
├── blackmagic/             # Upstream BMP submodule
│   └── src/
│       ├── target/         # All target support
│       └── ...
└── platformio.ini          # Build configuration
```

### Code Organization

**ESP32-specific (maintained locally):**
- WiFi/TCP and Serial GDB interfaces
- Platform initialization and GPIO
- Web server and WebSocket
- Runtime mode switching
- Custom monitor commands

**From upstream (git submodule):**
- All target chip support
- GDB protocol implementation
- RTT, SWO support
- Core debugging logic

See `MIGRATION_STATUS.md` for detailed integration status.

### Adding New Targets

Upstream targets are automatically included. To add custom targets:

1. Add `your_target.c` to `main/target/`
2. Update `main/CMakeLists.txt`:
   ```cmake
   set(CUSTOM_TARGET_SOURCES
       ${CMAKE_CURRENT_SOURCE_DIR}/target/your_target.c
   )
   ```
3. Implement probe function (see `main/target/esp32c3.c` for example)

## Version History

**January 2026 - Major Update:**
- Runtime WiFi/Serial mode switching via GPIO button
- Full Serial GDB support over UART0
- Optional debug UART for log isolation (GPIO26/32)
- M5Stack Atom Lite pin configuration
- Puya PY32F microcontroller support
- WiFi Access Point mode (default)
- Web UI with WebSocket support
- RTT support via upstream integration
- Major code refactoring with upstream submodule

**Upstream Integration:**
- Submodule: [blackmagic-debug/blackmagic](https://github.com/blackmagic-debug/blackmagic)
- Commit: ec31cd5 (January 2026)

## Resources

**Black Magic Probe:**
- [Official Documentation](https://black-magic.org/)
- [GitHub Repository](https://github.com/blackmagic-debug/blackmagic)
- [FAQ](https://github.com/blacksphere/blackmagic/wiki/Frequently-Asked-Questions)

**Debugging Guides:**
- [Using RTT](https://github.com/blackmagic-debug/blackmagic/blob/main/UsingRTT.md)
- [Using SWO](https://github.com/blackmagic-debug/blackmagic/blob/main/UsingSWO.md)
- [Orbuculum - SWO/SWV tools](https://github.com/orbcode/orbuculum)

**ESP32:**
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [PlatformIO ESP32](https://docs.platformio.org/en/latest/platforms/espressif32.html)

## License

GPL-3.0 (inherited from Black Magic Probe project)

## Contributing

Contributions welcome! Areas of interest:
- Additional board configurations
- Web UI improvements
- Documentation
- Testing on different ESP32 variants

Please test thoroughly and follow existing code style.

---
