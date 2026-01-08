# PDP-11/70 Front Panel Controller

Alternative front panel controller for PiDP-11 hardware using OpenSIMH's sim_frontpanel API.

## Installation

First, install required dependencies:
```bash
sudo apt-get update
sudo apt-get install libgpiod-dev build-essential git
```

### Get OpenSIMH Source Files

The frontpanel program requires `sim_frontpanel.c` and `sim_sock.c` from the OpenSIMH source:

```bash
# Clone OpenSIMH repository
git clone https://github.com/open-simh/simh.git

# Copy required files to frontpanel directory
cp /path/to/simh/sim_frontpanel.* /path/to/pidp-frontpanel
cp /path/to/simh/sim_sock.* /path/to/pidp-frontpanel
```

### Build and Install

```bash
make
sudo make install
```

This installs the `frontpanel` binary to its install location (default `/opt/pidp11`).

## Command-Line Usage

```bash
frontpanel [OPTIONS] <pdp11_binary> <config_file_full_path>

Options:
  -d, --daemon     Run as daemon with syslog logging
  -h, --help       Show help message
```

**Important:** Both the PDP-11 binary path and configuration file path must be **absolute paths**.

### Examples

**Normal mode (console output, use with `screen` or `tmux`):**
```bash
sudo /opt/pidp11/frontpanel /opt/simh/BIN/pdp11 /opt/pidp11/config.txt
```

**Daemon mode (syslog output):**
```bash
sudo /opt/pidp11/frontpanel --daemon /opt/simh/BIN/pdp11 /opt/pidp11/config.txt
```

## Configuration File Format

The configuration file maps switch register values to system configurations. Each line contains:

```
<switch_code_octal>, <full_directory_path>, <config_filename>, <boot_device>
```

**Example configuration file** (`/opt/pidp11/config.txt`):
```
0102, /opt/pidp11/systems/211bsd, boot.ini, rq0
0105, /opt/pidp11/systems/unix5, boot.ini, rk0
0106, /opt/pidp11/systems/unix6, boot.ini, rk0
0107, /opt/pidp11/systems/unix7, boot.ini, rp0
0113, /opt/pidp11/systems/sysiii, boot.ini, rp0
0115, /opt/pidp11/systems/sysv, boot.ini, rp0
0132, /opt/pidp11/systems/2.11BSD, boot.ini, ra0
```

**Format details:**
- `switch_code_octal`: Octal value read from front panel switches (12 bits)
- `full_directory_path`: **Absolute path** to system directory
- `config_filename`: SimH configuration file inside the system directory (usually `boot.ini`)
- `boot_device`: Device to boot from (e.g., `rp0`, `rk0`, `rq0`, `ra0`)

## SimH Configuration Requirements

Your SimH `boot.ini` file **must not** contain a `boot` command. Instead, configure the console and remote interfaces:

**Required configuration:**
```ini
; Console telnet (buffered mode for better performance)
set console telnet=buffered
set console -u telnet=3999

; Remote control interface
set remote -u telnet=1024

; Your device configuration
set cpu 11/70
...

; DO NOT include "boot rp0" - frontpanel handles booting
```

**Why this is required:**
- The front panel program boots the system automatically using the device specified in the config file
- The console telnet interface allows you to interact with the system
- The remote interface enables control and monitoring

## How to Use the Front Panel

**Overview**

1. **Set the switch register** to the octal code for your desired system (e.g., `0107` for Unix v7)
2. **Start the frontpanel program** - it will read the switches and load the matching configuration
3. The system will automatically boot the configured device

**If an invalid switch code selected:**
- The program will display an error and wait 10 seconds
- After waiting, it re-reads the switches and tries again

So, set switches to a valid code and wait for automatic retry.

**Changing systems without restarting:**
- **Method 1:** Set switches to new code, press **R2 button** to restart session
- **Method 2:** Edit config file, press **R1 button** to reload configuration

## Front Panel Controls

**Switch Register (22 bits):**
- Used to select which system to boot (matches configuration file)
- Used to enter addresses and data for examination/deposit operations
- Bottom 3 bits (SR0-SR2) select CPU register when R2 is in DISPLAY REGISTER position

**Control Switches:**

- **LOAD ADDR:** Load switch register value into console address register (only when halted)
- **EXAM:** Examine memory at console address, then increment:
  - By 2 for normal memory addresses
  - By 1 for register space (017777700-017777717)
- **DEP:** Deposit switch register value to console address, then increment:
  - By 2 for normal memory addresses
  - By 1 for register space (017777700-017777717)
- **CONT:** Single-step execution (ignores of "S INST" / "S BUS CYCLE" switch)
- **ENABLE/HALT:** Toggle between running and halted states
  - When switching to HALT: sets console address to current PC
  - When switching to ENABLE: disables console address display
- **S_INST/S_BUS_CYCLE:** Non-functional (CONT always single-steps)
- **START:** Set PC to console address
  - If ENABLE mode: also starts execution
  - If HALT mode: only sets PC without running
- **TEST:** Dump all panel state to log (for debugging)

**Rotary Encoders:**

- **R1 (8 positions):** Select address display source
  - When CPU is **running**:
    - Positions 0-2: USER_D, SUPER_D, KERNEL_D: shows PC (16-bit virtual address)
    - Position 3: CONS_PHY: shows PC (16-bit) combined with upper 6 bits from switch register
    - Positions 4-6: USER_I, SUPER_I, KERNEL_I: shows PC (16-bit virtual address)
    - Position 7: PROG_PHY: shows PC (full 22-bit physical address)
  - When CPU is **halted**:
    - Positions 0-2, 4-6: USER_D, SUPER_D, KERNEL_D, USER_I, SUPER_I, KERNEL_I: shows console address (16-bit virtual address)
    - Positions 3, 7: CONS_PHY, PROG_PHY: shows console address (22-bit physical address)

- **R2 (4 positions):** Select data/display mode
  - Position 0: DATA_PATHS (ALU/SHIFTER output)
    - During EXAM/DEP: shows examined/deposited value
    - When halted (no EXAM/DEP): shows R0
    - When running: shows last value (unchanged)
  - Position 1: BUS_REG
    - When halted: shows switch register value
    - When running: shows instruction register (IR)
  - Position 2: MU_ADR_FPP_CPU (microaddress)
    - Not available (shows 0x0000)
  - Position 3: DISPLAY_REGISTER: shows CPU register selected by SR[2:0]
    - SR[2:0] = 0-5: R0-R5
    - SR[2:0] = 6: SP (stack pointer)
    - SR[2:0] = 7: PC (program counter, low 16 bits)

**Rotary Buttons:**

- **R1 Button:** Reload configuration file and restart
- **R2 Button:** Restart session

In both restarts, the switch position indicates which system to launch next.

### LED Indicators

**Address LEDs (22 bits):**
- When CPU is running: displays address selected by R1 rotary encoder (with blinkenlights effect showing bus activity)
- When CPU is halted: displays console address filtered by R1 position (16-bit or 22-bit)

**Data LEDs (16 bits):**
- Display depends on R2 rotary encoder position (see R2 positions above)

**Status LEDs:**
- **ADDR 16:** MMU disabled (MMR0 bit 0 clear)
- **ADDR 18:** 18-bit addressing mode (MMU enabled, MMR3 bit 4 clear)
- **ADDR 22:** 22-bit addressing mode (MMU enabled, MMR3 bit 4 set)
- **KERNEL/SUPER/USER:** Current CPU mode (derived from PSW bits 15:14)
- **DATA:** Processor is performing data access (vs instruction fetch)
- **MASTER:** CPU is halted
- **RUN:** CPU is executing instructions
- **PAUSE:** (reserved)

**Parity LEDs:**
- **PAR LOW/HIGH:** Even parity indicators for low/high byte of data
- Only active when displaying static data in DATA_PATHS or BUS_REG modes
- Not shown during blinkenlights (when CPU is running)

### Typical Usage Example

**Load and examine memory:**
```
1. Set switches to desired address (e.g., 0107)
2. Press LOAD ADDR
3. Press EXAM repeatedly to examine sequential locations
4. Data LEDs show memory contents
```

**Deposit data to memory:**
```
1. Set switches to address
2. Press LOAD ADDR
3. Set switches to data value
4. Press DEP
5. Repeat steps 3-4 to deposit sequential locations
```

**Boot a different system:**
```
1. Set switches to system code (e.g., 0105 for Unix v5)
2. Press R2 button
3. System will restart with new configuration
```

## Connecting to SimH Console

The front panel starts SimH with a telnet console on port 3999:

```bash
telnet localhost 3999
```

## Running as a Systemd Service

Create `/etc/systemd/system/frontpanel.service`:

```ini
[Unit]
Description=PDP-11/70 Front Panel Controller
After=network.target
Wants=network.target

[Service]
Type=forking
ExecStart=/opt/pidp11/frontpanel --daemon /opt/simh/BIN/pdp11 /opt/pidp11/config.txt
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**Enable and start the service:**
```bash
sudo systemctl daemon-reload
sudo systemctl enable frontpanel
sudo systemctl start frontpanel
```

**View logs:**
```bash
# View all logs
sudo journalctl -u frontpanel -f

# View only errors
sudo journalctl -u frontpanel -p err

# View recent logs
sudo journalctl -u frontpanel -n 50
```

**Control the service:**
```bash
sudo systemctl status frontpanel    # Check status
sudo systemctl stop frontpanel       # Stop service
sudo systemctl restart frontpanel    # Restart service
```

**Debug Mode:**

Press the TEST switch on the front panel (the white switch between the address and control switches) to dump complete state information to the log, including:
- All edge detector states
- Switch register value
- All control switch states
- Rotary encoder positions
- Raw 3×12 switch matrix

## License

See LICENSE file for details.
