- [Overview](#overview)
  - [Supported Features](#supported-features)
- [Installation Instructions](#installation-instructions)
  - [Install using DKMS](#install-using-dkms)
  - [Installing on TrueNAS Scale](#installing-on-truenas-scale)
    - [Compiling using Nader Eloshaiker's Docker container](#compiling-using-nader-eloshaikers-docker-container)
    - [Install Procedure](#install-procedure)
    - [Uninstall Procedure](#uninstall-procedure)
  - [Autoload on startup with Systemd](#autoload-on-startup-with-systemd)
  - [Removing the module](#removing-the-module)
- [How to use this module](#how-to-use-this-module)
  - [Module Parameter](#module-parameter)
  - [Card Info (sysfs)](#card-info-sysfs)
  - [Fan Reporting/Control and Temperature Sensors](#fan-reportingcontrol-and-temperature-sensors)
  - [Slot LED Control](#slot-led-control)
- [Supported Models](#supported-models)
- [Questions and Answers](#questions-and-answers)


## Overview

The qm2mod project is a kernel module for exposing the functionality of QNAP QM2 expansion cards vi standard Linux APIs and subsystems. This is intended for QNAP NASes that are not running the original Q(u)TS operating system, this module should also work for anyone running a QM2 card in a standard PC (which I believe QNAP do not supply any software for this kind of setup).

Each QM2 card contains a PCIe switch and a PIC microcontroller connected via an I2C bus that is bit-banged over GPIO pins on the PCIe switch chip. This module creates that I2C bus internally and uses it to communicate with the PIC, exposing card functionalities to the OS.

This is a sister project of my [qnap8528](https://github.com/0xGiddi/qnap8528/) IT8528 EC driver project.

This project has no affiliation with *QNAP Systems Inc.*.

### Supported Features

✅ Temperature sensors via hwmon (2 sensors on 2-slot cards, 4 on 4-slot cards)\
✅ Fan RPM reporting via hwmon\
✅ Fan PWM control via hwmon\
✅ Per-slot LED control via the Linux LED subsystem (red fault LED, green present LED, per slot)\
✅ Card model name and serial number via sysfs\
✅ PIC microcontroller firmware version via sysfs\
✅ Fan reset to PIC built-in control/default-value via sysfs\

> **Note:** The **QM2-2S10G1TB02** is not supported by this module. It uses the IT8528 Embedded Controller rather than an I2C bus on the PCIe switch chip, making it a fundamentally different architecture that requires a separate driver (qnap8528 does not support QM2 cards as of now).


## Installation Instructions

Before installing, check the *Supported Models* table to confirm your card is compatible. The following instructions have been tested on *Debian 12 (bookworm) x64*.

**Disclaimer:** This kernel module is provided as-is, without any warranty of functionality or fitness for a specific purpose. I accept no liability for any damage, data loss, or system instability resulting from its use. Use at your own risk.

**NOTE:** The module depends on the standard `i2c-algo-bit` kernel module for I2C bit-banging. 

### Install using DKMS

1. Clone the repository: `git clone https://github.com/0xGiddi/qnap-qm2.git`
2. Enter the project directory: `cd qm2mod`
3. Compile and install using DKMS: `sudo make install`
4. Verify the installation: `dkms status`
5. Load the module: `sudo modprobe qm2mod`

### Installing on TrueNAS Scale

> **❗ Important:** Updates to the TrueNAS OS will overwrite any changes made during installation, requiring the process to be repeated.

#### Compiling using Nader Eloshaiker's Docker container

Visit [https://github.com/nader-eloshaiker/truenas-qnap-qm2-module](https://github.com/nader-eloshaiker/truenas-qnap-qm2-module) and follow the instructions to build the module.
This method has the benefit that it does not require a local shell and does not require enabling TrueNAS "Developer-Mode"
> Note: The repository and code linked above in this section are not controlled by me and should be verified independently.

#### Install Procedure

> **❗ Important:** TrueNAS Scale is a highly restricted operating system that does not support modifications to the host OS environment. Installing this module requires enabling **Developer Mode**, which allows installation of build tools and modification of the root filesystem. However, enabling Developer Mode voids official support from iXsystems. For more information, refer to the [TrueNAS documentation](https://www.truenas.com/docs/scale/scaletutorials/systemsettings/advanced/developermode/).


1. Connect to TrueNAS via the web console, SSH, or the local Linux shell.
2. Run `sudo install-dev-tools` to disable read protection on the root filesystem and install required build tools.
3. Download the latest source tarball from the [releases page](https://github.com/0xGiddi/qnap-qm2/releases/latest) and extract it: `tar xzf <filename>`.
4. Enter the source directory: `cd qnap-qm2-<version>/src`
5. Run `make` — **without `sudo`** (as `truenas_admin` or `root` if using the local console).
6. Confirm the build succeeded: `echo $?` should output `0` and that `qm2mod.ko` was created.
7. Copy the kernel module to the Linux modules directory: `sudo cp qm2mod.ko /lib/modules/$(uname -r)/extra/`
8. Update the module database: `sudo depmod -a`
9. The module is now installed. Follow [Autoload on startup with Systemd](#autoload-on-startup-with-systemd) or load it manually with `modprobe qm2mod`.

#### Uninstall Procedure

1. Unload the module: `modprobe -r qm2mod`
2. Delete the module file: `rm /lib/modules/$(uname -r)/extra/qm2mod.ko`
3. Update the module database: `depmod -a`

### Autoload on startup with Systemd

1. Create a new unit file: `touch /etc/systemd/system/qm2mod-load.service`
2. Open the file and add the following content:
```ini
[Unit]
Description=Load qm2mod QM2 expansion card kernel module

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/sbin/modprobe qm2mod
ExecStop=/sbin/modprobe -r qm2mod

[Install]
WantedBy=multi-user.target
```
3. Enable and start the service: `systemctl enable --now qm2mod-load.service`
4. Verify the module loaded: `lsmod | grep qm2mod`

If the module is not loaded, try `systemctl daemon-reload` followed by `systemctl start qm2mod-load.service`.

### Removing the module

1. Stop and disable the service: `systemctl disable --now qm2mod-load.service`
2. Delete the unit file: `rm /etc/systemd/system/qm2mod-load.service`
3. Uninstall using DKMS: `sudo make uninstall`
4. Verify removal: `dkms status`


## How to use this module

### Module Parameter

`preserve_leds`:\
Set to `true` by default. When enabled, the module does **not** change the LED state on unload, preserving whatever was last set. Set to `false` if you want all slot LEDs to turn off when the module is removed.


### Card Info (sysfs)
Card information is exposed under `/sys/devices/platform/qm2.N/`, where `N` is the card index (0-based, incremented per card found at module load time). The following sysfs attributes are available:

| Attribute | Access | Description |
|-|-|-|
| `model` | Read | Card model string, e.g. `QM2-4P-384` |
| `serial` | Read | Card serial number |
| `pic_version` | Read | PIC firmware version, e.g. `0.10` |
| `fan_default` | Write | Write `1` to restore factory fan control/value |


### Fan Reporting/Control and Temperature Sensors

Fan and temperature sensors are exposed via the standard Linux hwmon subsystem, visible under `/sys/class/hwmon/hwmonX/` (where `X` is assigned dynamically at load time).

To find which hwmon device belongs to your QM2 card:
```
grep -rl "qm2" /sys/class/hwmon/*/name
```

| Attribute | Description |
|-|-|
| `tempX_input` | Temperature in millidegrees Celsius. `X` goes from 1 to 2 (2-slot cards) or 1 to 4 (4-slot cards) |
| `fan1_input` | Fan speed in RPM |
| `pwm1` | Fan PWM duty cycle, 0–255 (read/write) |

**Note:** This module only exposes the fan control interface, it does not implement any automatic fan control. Fan speed management is left to the user or a tool such as `fancontrol` from the `lm-sensors` package. Writing `1` to the `fan_default` sysfs attribute hands fan control back to the PIC, I **do not** know if the PIC has a built in fan control based on temperature or it's a static value. 


### Slot LED Control

**NOTE - for anyone familiar with my qnap8528 module:** Unlike the qnap8528 module, each color has its own LED entry and the brightness control determines solid or blinking, this is due to the hardware architecture. Unlike EC LEDs, you can have both the green and red LEDs illuminated at the same time (tough the red is overpowering). 

Each M.2 slot has two LEDs exposed through the Linux LED subsystem: a **red** LED (error indicator) and a **green** LED (present/activity indicator). LEDs are named using the format:

```
qm2-<adapter>:<color>:slot<N>
```

For example, on adapter 32 with 2 slots: `qm2-32:red:slot1`, `qm2-32:green:slot1`, `qm2-32:red:slot2`, `qm2-32:green:slot2`.

The adapter number is the I2C adapter number assigned at module load time, see [How do I find my adapter number?](#questions-and-answers).

LEDs are found under `/sys/class/leds/`.

**Supported brightness values:**

| LED | Brightness | Effect |
|-|-|-|
| `red` | `0` | Off |
| `red` | `1` | On (red) |
| `red` | `2` | Blink (red) |
| `green` | `0` | Off |
| `green` | `1` | On (green) |
| `green` | `2` | On (green) |

Standard Linux LED triggers (`heartbeat`, `timer`, `oneshot`, etc.) should work with these LEDs.

## Supported Models

All models in the table below use an I2C bus on their PCIe switch chip and are supported by this module. I have not tested them all, I only have tested the QM2-4P-384 and partial tests on a QM2-2P10G1TB. Different cards have different PCI interfaces and I hope I implemented them all correctly (I only can test ASM2824 based devices).

> **Note on "generic" QM2-2P cards:** Any QM2-2P model *not* in the explicit list below has M.2 slots 1 and 2 physically swapped on the PCIe switch. The driver detects this automatically and compensates, so LED and temperature sensor numbering matches the physical slot labels on the card bracket (that what I gather from the software reverse engineering efforts)."Generic" means that the card may pass the PCI checks and work properly, but does not have an explicit entry in the QM2 card list by QNAP.

| Model | M.2 Slots | Fan | Notes |
|-|-|-|-|
| QM2-2P10G1T | 2 | ✅ | 
| QM2-2P10G1TA | 2 | ✅ | 
| QM2-2P10G1TB | 2 | ✅ | 
| QM2-2P-344  | 2 | ✅ | 
| QM2-2P-384  | 2 | ✅ | 
| QM2-2P-344A  | 2 | ✅ | 
| QM2-2P-384A  | 2 | ✅ | 
| QM2-2P2G2T | 2 | ✅ | 
| QM2-2P410G1T  | 2 | ✅ | 
| QM2-2P410G2T  | 2 | ✅ |
| QM2-2P-244A-A1  | 2 | ✅ | 
| QM2-4P-384  | 4 | ✅ | 
| QM2-2S10G1TB02 | — | — |  ❌ **Not supported**, EC-based architecture |
| "Generic" QM2-2P| 2 | ✅ | Slots 1,2 swapped in hardware, corrected by driver |
| "Generic" QM2-4P|4 | ✅ | 


As mentioned, if your card is not listed above, it may still work if it uses one of the supported PCIe switch chips. Load the module and check `dmesg | grep qm2mod` for a match or error message. If your card works, please open an issue so it can be added to the table (and vice versa, if a "supported" card does not work, please let me know).


## Questions and Answers

**Q. My QM2 model is not in the supported table — will it still work?**\
**A.** *Possibly. The driver matches on PCI vendor/device IDs of the PCIe switch chip rather than the QM2 model string. If your card uses one of the supported switch chips (ASM2824/1812/2812, IDT, PLX PEX8714/8718, Tehuti 4027, PEX88000, ASM0625), the driver may detect it. Load the module and run `dmesg | grep qm2mod`. If you see "matched" and "Attached" messages, it worked. Please open an issue to report it so it can be added to the supported list.*

**Q. Why is the QM2-2S10G1TB02 not supported?**\
**A.** *This card is based on an Aquantia/Marvell AQC NIC instead of a PCIe switch, and there are no usable GPIO pins on the NIC for I2C bit-banging (my guess). The PIC on this card is wired directly to the QNAP NAS motherboard's IT8528 Embedded Controller and is only accessible via EC I/O ports, a completely different interface. Supporting it would require a separate driver that communicates with the EC instead of bit-banging I2C. If you would like to contribute to adding support, please open an issue so I can try and implement it n the qnap8528 module*


**Q. The fan speed does not change with temperature automatically — is that normal?**\
**A.** *Yes, this is expected. This module exposes the fan PWM interface but does not implement any fan curve or automatic control. Use a tool like `fancontrol` (from the `lm-sensors` package) to create an automatic control policy, or write `1` to the `fan_default` sysfs attribute to hand control back to the PIC (which **might** be smart, but I have no proof it actually has an algorithm)*

**Q. How do I find my adapter number (the number in `qm2-X:...`)?**\
**A.** *Run `dmesg | grep "qm2mod.*Attached"`, the output includes `i2c=N` which gives the adapter number. Alternatively, check `cat /sys/class/i2c-adapter/i2c-N/name` for each adapter until you find one containing "qm2".* (`grep -H "qm2" /sys/class/i2c-adapter/*/name | cut -d'/' -f5 | cut -d'-' -f2` will return the adapter number)

**Q. I need feature XYZ — can you add it?**\
**A.** *Depends on the feature and whether it falls within the PIC's I2C command set. Create an issue with as much detail as possible (card model, what you want to control, any hardware research you've done) and we can look at it.* Example scripts, sidecars and packages may be provided later, request them so I know there is demand. I doubt there is an actual QM2 card feature that was not covered. 
