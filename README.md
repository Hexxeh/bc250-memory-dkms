# AMD BC-250 GDDR6 temperature hwmon driver

Linux kernel hwmon driver for monitoring GDDR6 memory temperatures on the **AMD / ASRock BC-250**.

Credit to github.com/pan-Rijovich/bc250-memory-temperature for originally developing this approach - this repo re-uses the same SMU patch to provide data via a kernel module rather than a Python script.

## Overview

- Communicates with the AMD BC-250 SMU via the Host Bridge (`0000:00:00.0`) SMN aperture.
- Automatically writes the custom 176-byte SMU payload and hooks Queue 3 Message 5 if not already active (assumes debug access is enabled by BIOS).
- Reads JEDEC MR3 DRAM temperature codes from all 8 GDDR6 chips.
- Calculates and exposes **VRAM Hotspot** and **VRAM Average** temperatures.
- Integrates into standard Linux `hwmon` sysfs (`sensors`).
- 1-second temperature caching avoids redundant SMU round-trips when reading sysfs attributes.

## Building and Installing

### Manual Build

```bash
make LLVM=1 modules
```

### Loading the Driver

```bash
sudo insmod bc250_memory.ko
```

Check `dmesg` to verify:
```bash
dmesg | tail
```

View temperatures:
```bash
sensors bc250_memory-*
```

Example `sensors` output:
```text
bc250_memory-isa-0000
Adapter: ISA adapter
VRAM Hotspot:  +44.0°C  
VRAM Average:  +38.5°C  
VRAM Chip 0:   +36.0°C  
VRAM Chip 1:   +34.0°C  
VRAM Chip 2:   +44.0°C  
VRAM Chip 3:   +36.0°C  
VRAM Chip 4:   +36.0°C  
VRAM Chip 5:   +42.0°C  
VRAM Chip 6:   +42.0°C  
VRAM Chip 7:   +38.0°C  
```

### DKMS Installation

```bash
sudo cp -r . /usr/src/bc250-memory-1.0.0
sudo dkms add -m bc250-memory -v 1.0.0
sudo dkms build -m bc250-memory -v 1.0.0
sudo dkms install -m bc250-memory -v 1.0.0
```

## Module Parameters

- `auto_patch` (bool, default `true`): Automatically inject the SMU payload and hook Queue 3 Message 5 if not already active.
- `force` (bool, default `false`): Force loading even if DMI does not match `AMD BC-250`.

## License and Attribution

- Driver code is licensed under the **GNU General Public License v2.0 or later** (`GPL-2.0-or-later`).
- The embedded `smu_payload` binary is derived from [bc250-memory-temperature](https://github.com/pan-Rijovich/bc250-memory-temperature) and is licensed under the **MIT License**:

  ```text
  Copyright (c) 2026 pan-Rijovich, bc250-collective

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  ```

The kernel module identifies as `Dual MIT/GPL`.

