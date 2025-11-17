# UEFI Support Implementation Plan for Bochs

## Executive Summary

This document outlines a comprehensive plan to add UEFI firmware support to the Bochs x86 PC emulator, enabling it to boot OVMF (Open Virtual Machine Firmware) from the EDK2 project. Currently, Bochs only supports legacy BIOS boot, while modern virtual machines and operating systems increasingly require UEFI boot capabilities.

**Status**: OVMF currently does not work in Bochs and is written primarily for QEMU. This implementation will require significant architectural changes to Bochs core.

**Parent Repository**: https://github.com/bochs-emu/Bochs

**Related GitHub Issues**:
- [#560](https://github.com/bochs-emu/Bochs/issues/560) - **ACPI is faulty when booting UEFI** (Open) ⚠️ CRITICAL
- [#265](https://github.com/bochs-emu/Bochs/issues/265) - Add TPM 1.2, 2.0 emulation, UEFI with Secure Boot (Open)
- [#471](https://github.com/bochs-emu/Bochs/issues/471) - UEFI boot (Closed - Fixed)
- [#555](https://github.com/bochs-emu/Bochs/issues/555) - Panic on CET setting dirty flag (Closed)

---

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [UEFI/OVMF Requirements](#uefiovmf-requirements)
3. [Required Changes](#required-changes)
4. [Implementation Phases](#implementation-phases)
5. [Technical Specifications](#technical-specifications)
6. [Challenges and Risks](#challenges-and-risks)
7. [Testing Strategy](#testing-strategy)
8. [References](#references)

---

## Current State Analysis

### Existing Bochs Architecture

**BIOS Implementation** (`bochs/memory/misc_mem.cc:296`):
- ROM loaded at `0xFFFF0000` (last 64KB of 32-bit address space)
- Maximum BIOS ROM size: 4MB (`BIOSROMSZ = 1 << 22`)
- Expansion ROM size: 128KB (`EXROMSIZE = 0x20000`)
- Boot sequence: CPU starts at `CS=0xF000, EIP=0x0000` (physical `0xFFFF0000`)

**Supported BIOS**:
- BIOS-bochs-latest (legacy Bochs BIOS)
- SeaBIOS (legacy BIOS compatible)
- i440fx.bin (custom legacy BIOS for i440FX chipset)

**Chipset Support** (`bochs/iodev/pci.cc`):
- i430FX TSC/TDP (PCI Device ID: 0x8086:0x0122)
- i440FX PMC/DBX (PCI Device ID: 0x8086:0x1237)
- i440BX Host Bridge (PCI Device ID: 0x8086:0x7190 or 0x7192)

**Memory Limitations**:
- i430FX: Maximum 128MB RAM
- i440FX/i440BX: Maximum 1024MB RAM
- Current BIOS ROM address: `0xFFFF0000`

**ACPI Support** (`bochs/iodev/acpi.cc`):
- PIIX4 ACPI controller implemented
- ACPI debug port: `0xB044`
- DSDT table defined in `bochs/bios/acpi-dsdt.dsl`
- PM base registers configured via PCI config space (offset 0x40)

**Missing Components**:
- ❌ No fw_cfg device (QEMU's firmware configuration interface)
- ❌ No UEFI/EFI support code
- ❌ No Q35 chipset emulation
- ❌ No dynamic ACPI table generation for firmware
- ❌ No SMBIOS table generation
- ❌ Memory layout incompatible with OVMF requirements

---

## UEFI/OVMF Requirements

### OVMF Architecture

**Minimum Requirements**:
- 64MB RAM minimum (128MB recommended)
- Reset vector at `0xFFFF_FFF0`
- Firmware device located just below 4GB boundary
- PCI chipset (i440FX or Q35)
- Cirrus SVGA (PCI mode, not ISA)

**Memory Layout** (for 2MB OVMF image):
```
0xFFE00000-0xFFFFFFFF  (2MB below 4GB)
├── 0xFFE00000: NV Variable Storage (56KB)
├── 0xFFE0E000: Event Log Area (4KB)
├── 0xFFE0F000: FTW Work Block (4KB)
├── 0xFFE10000: FTW Spare Blocks (64KB)
├── 0xFFE20000: FVMAIN_COMPACT (compressed firmware ~1.5MB)
└── 0xFFFCD000: SECFV (SEC firmware + reset vector, 208KB)
    └── 0xFFFFFFF0: Reset vector (jumps to SEC entry)
```

**For 4MB OVMF image**:
```
0xFFC00000-0xFFFFFFFF  (4MB below 4GB)
```

**Boot Process**:
1. CPU executes reset vector at `0xFFFFFFF0`
2. SEC (Security) phase runs from flash
3. SEC decompresses FVMAIN_COMPACT from flash to RAM
4. PEI (Pre-EFI Initialization) phase executes
5. DXE (Driver Execution Environment) phase loads drivers
6. BDS (Boot Device Selection) phase boots OS

**Required Platform Features**:
- fw_cfg device for configuration/ACPI table passing
- Dynamic ACPI table generation (DSDT, FADT, MADT, etc.)
- SMBIOS tables
- E820 memory map via fw_cfg
- SMM support (optional, but recommended)
- Boot order configuration via fw_cfg

---

## Required Changes

### 1. fw_cfg Device Implementation

**Priority**: CRITICAL

**File**: `bochs/iodev/fwcfg.cc` (new), `bochs/iodev/fwcfg.h` (new)

**I/O Ports**:
- `0x510`: Selector register (16-bit, write-only)
- `0x511`: Data register (8-bit, read/write)
- `0x514`: DMA address register (32-bit)

**Key Selectors to Implement**:
```c
#define FW_CFG_SIGNATURE     0x0000  // Returns "QEMU" (4 bytes)
#define FW_CFG_ID            0x0001  // Device ID
#define FW_CFG_UUID          0x0002  // VM UUID
#define FW_CFG_RAM_SIZE      0x0003  // RAM size in MB
#define FW_CFG_NOGRAPHIC     0x0004  // Graphics mode
#define FW_CFG_NB_CPUS       0x0005  // Number of CPUs
#define FW_CFG_MACHINE_ID    0x0006  // Machine ID
#define FW_CFG_KERNEL_ADDR   0x0007  // Kernel load address
#define FW_CFG_KERNEL_SIZE   0x0008  // Kernel size
#define FW_CFG_KERNEL_CMDLINE 0x0009 // Kernel command line
#define FW_CFG_INITRD_ADDR   0x000a  // Initrd address
#define FW_CFG_INITRD_SIZE   0x000b  // Initrd size
#define FW_CFG_BOOT_DEVICE   0x000c  // Boot device
#define FW_CFG_NUMA          0x000d  // NUMA data
#define FW_CFG_BOOT_MENU     0x000e  // Boot menu config
#define FW_CFG_MAX_CPUS      0x000f  // Maximum CPUs
#define FW_CFG_KERNEL_ENTRY  0x0010  // Kernel entry point
#define FW_CFG_KERNEL_DATA   0x0011  // Kernel data
#define FW_CFG_INITRD_DATA   0x0012  // Initrd data
#define FW_CFG_CMDLINE_ADDR  0x0013  // Command line address
#define FW_CFG_CMDLINE_SIZE  0x0014  // Command line size
#define FW_CFG_CMDLINE_DATA  0x0015  // Command line data
#define FW_CFG_SETUP_ADDR    0x0016  // Setup address
#define FW_CFG_SETUP_SIZE    0x0017  // Setup size
#define FW_CFG_SETUP_DATA    0x0018  // Setup data
#define FW_CFG_FILE_DIR      0x0019  // File directory
```

**File-based Interface**:
```c
struct FWCfgFile {
    uint32_t size;        // Big-endian
    uint16_t select;      // Big-endian
    uint16_t reserved;
    char name[56];        // NUL-terminated
};
```

**Critical fw_cfg Files for OVMF**:
- `etc/acpi/tables` - ACPI tables (DSDT, FADT, MADT, etc.)
- `etc/acpi/rsdp` - ACPI RSDP pointer
- `etc/table-loader` - ACPI table loader commands
- `etc/smbios/smbios-tables` - SMBIOS data
- `etc/e820` - E820 memory map
- `etc/boot-cpus` - SMP CPU information
- `bootorder` - Boot device order

### 2. Memory Layout Changes

**Priority**: CRITICAL

**Files**:
- `bochs/memory/memory.h`
- `bochs/memory/memory.cc`
- `bochs/memory/misc_mem.cc`

**Changes Required**:

1. **Extend ROM size support**:
```cpp
// Current: BIOSROMSZ = 4MB (1 << 22)
// Change to support 4MB OVMF:
#define BIOSROMSZ_UEFI (1 << 22)  // 4MB for UEFI firmware
#define BIOS_ROM_BASE_UEFI 0xFFC00000  // 4MB below 4GB
```

2. **Configurable ROM base address**:
```cpp
// bochs/memory/misc_mem.cc:68
// Current: BX_MEM_THIS bios_rom_addr = 0xffff0000;
// Add support for:
BX_MEM_THIS bios_rom_addr = 0xFFC00000; // for 4MB OVMF
// or
BX_MEM_THIS bios_rom_addr = 0xFFE00000; // for 2MB OVMF
```

3. **Reset vector relocation**:
```cpp
// Ensure reset vector at 0xFFFFFFF0 correctly jumps to OVMF SEC
// This is at offset (4MB - 16 bytes) in the ROM
```

4. **RAM size increase**:
```cpp
// bochs/iodev/pci.cc:173
// Current i440FX max: 1024MB
// Ensure support for larger RAM configurations
if (ramsize > 4096) ramsize = 4096; // Support up to 4GB
```

### 3. ACPI Table Generation

**Priority**: HIGH

**Files**:
- `bochs/iodev/acpi.cc` (modify)
- `bochs/iodev/acpigen.cc` (new)
- `bochs/iodev/acpigen.h` (new)

**Required ACPI Tables**:

1. **RSDP** (Root System Description Pointer)
2. **RSDT** (Root System Description Table)
3. **FADT** (Fixed ACPI Description Table)
4. **MADT** (Multiple APIC Description Table) - for SMP
5. **DSDT** (Differentiated System Description Table)
6. **SSDT** (Secondary System Description Table) - optional
7. **HPET** (High Precision Event Timer)
8. **MCFG** (Memory-Mapped Configuration) - for PCI Express

**Implementation Approach**:
- Generate ACPI tables dynamically based on VM configuration (like QEMU does)
- Pass tables to OVMF via fw_cfg interface (`etc/acpi/tables`)
- Implement table loader interface for complex table linking

**Key ACPI Devices to Declare**:
```asl
// fw_cfg device
Device (FWCF) {
    Name (_HID, "QEMU0002")
    Name (_CRS, ResourceTemplate() {
        IO (Decode16, 0x0510, 0x0510, 0x01, 0x0C)
    })
}
```

### 4. SMBIOS Table Generation

**Priority**: MEDIUM

**Files**:
- `bochs/iodev/smbios.cc` (new)
- `bochs/iodev/smbios.h` (new)

**Required SMBIOS Structures**:
- Type 0: BIOS Information
- Type 1: System Information
- Type 2: Base Board Information
- Type 3: System Enclosure
- Type 4: Processor Information
- Type 16: Physical Memory Array
- Type 17: Memory Device
- Type 19: Memory Array Mapped Address
- Type 32: System Boot Information

**Delivery Method**:
- Generate SMBIOS entry point and tables
- Pass via fw_cfg: `etc/smbios/smbios-tables`
- OVMF's SmbiosPlatformDxe driver will install them

### 5. Device Presentation Changes

**Priority**: MEDIUM

**Files**:
- `bochs/iodev/devices.cc`
- `bochs/iodev/pci.cc`

**Changes**:

1. **PCI Device Configuration**:
   - Ensure all PCI devices present proper configuration space
   - Implement proper BAR (Base Address Register) handling
   - Support PCIe configuration space (MMCONFIG)

2. **Chipset Identification**:
   - OVMF expects to see QEMU-compatible chipset IDs
   - May need to add compatibility mode to present as QEMU i440FX

3. **Device Ordering**:
   - Match QEMU's device enumeration order
   - Important for OVMF's platform-specific code

### 6. Configuration Changes

**Priority**: LOW

**Files**:
- `bochs/config.cc`
- `bochs/.bochsrc`

**New Configuration Options**:
```
# ROM image configuration
romimage: file=$BXSHARE/bios/OVMF.fd, address=0xffc00000, type=uefi

# Firmware configuration
firmware: type=uefi               # uefi or legacy (default: legacy)
firmware: smm=1                   # Enable SMM support (default: 0)
firmware: secure_boot=0           # Enable Secure Boot (default: 0)

# fw_cfg device
fwcfg: enabled=1                  # Enable fw_cfg device (default: 0)
fwcfg: file="bootorder", data="1\n2\n3\n"  # Custom fw_cfg files
```

### 7. Q35 Chipset Support (Optional but Recommended)

**Priority**: LOW (Phase 2)

**Why Q35?**
- More modern architecture
- Better PCIe support
- Required for some OVMF features
- ICH9 southbridge vs PIIX3/PIIX4

**Files**:
- `bochs/iodev/q35.cc` (new)
- `bochs/iodev/q35.h` (new)
- `bochs/iodev/ich9.cc` (new)

**Note**: OVMF works with i440FX, so Q35 is not initially required.

---

## Implementation Phases

### Phase 1: Foundation (Weeks 1-4)

**Goal**: Implement core infrastructure for UEFI support

**Tasks**:

1. **Week 1: fw_cfg Device Implementation**
   - [ ] Create `bochs/iodev/fwcfg.cc` and `fwcfg.h`
   - [ ] Implement I/O port handlers (0x510, 0x511, 0x514)
   - [ ] Implement basic selectors (SIGNATURE, ID, RAM_SIZE)
   - [ ] Implement FILE_DIR selector and file-based interface
   - [ ] Add plugin registration and initialization
   - [ ] Unit test: Verify fw_cfg responds with "QEMU" signature

2. **Week 2: Memory Layout Restructuring**
   - [ ] Modify ROM base address configuration
   - [ ] Support 2MB and 4MB ROM sizes
   - [ ] Implement configurable ROM loading at 0xFFE00000 or 0xFFC00000
   - [ ] Verify reset vector at 0xFFFFFFF0 works correctly
   - [ ] Test: Load OVMF binary and verify memory mapping

3. **Week 3: E820 Memory Map**
   - [ ] Implement E820 map generation in fw_cfg
   - [ ] Export via `etc/e820` fw_cfg file
   - [ ] Support proper memory regions:
     - Usable RAM
     - Reserved (BIOS areas)
     - ACPI tables
     - ACPI NVS
   - [ ] Test: Verify memory map matches VM configuration

4. **Week 4: Boot Configuration**
   - [ ] Implement boot order via fw_cfg (`bootorder` file)
   - [ ] Implement CPU count via fw_cfg (NB_CPUS selector)
   - [ ] Implement UUID generation
   - [ ] Test: Boot OVMF with basic configuration

**Deliverable**: Bochs can load OVMF firmware and execute SEC phase

### Phase 2: ACPI Table Generation (Weeks 5-8)

**Goal**: Provide OVMF with complete ACPI tables

**Tasks**:

1. **Week 5: ACPI Infrastructure**
   - [ ] Create `bochs/iodev/acpigen.cc` for dynamic table generation
   - [ ] Implement RSDP generation
   - [ ] Implement RSDT generation
   - [ ] Implement table checksum calculation
   - [ ] Test: Generate valid empty ACPI tables

2. **Week 6: Core ACPI Tables**
   - [ ] Implement FADT generation
   - [ ] Implement DSDT compilation and inclusion
   - [ ] Add fw_cfg device to DSDT (QEMU0002)
   - [ ] Test: OVMF discovers ACPI tables via fw_cfg

3. **Week 7: MADT and SMP Support**
   - [ ] Implement MADT (APIC) table generation
   - [ ] Support multiple Local APICs (for SMP)
   - [ ] Support I/O APIC configuration
   - [ ] Test: Boot SMP configuration (2-4 CPUs)

4. **Week 8: Table Loader Interface**
   - [ ] Implement `etc/table-loader` fw_cfg file
   - [ ] Support ALLOCATE, ADD_POINTER, ADD_CHECKSUM commands
   - [ ] Support WRITE_POINTER command
   - [ ] Test: Complex inter-table references work

**Deliverable**: OVMF successfully loads and uses ACPI tables from Bochs

### Phase 3: SMBIOS and Device Support (Weeks 9-10)

**Goal**: Complete platform information for OVMF

**Tasks**:

1. **Week 9: SMBIOS Implementation**
   - [ ] Create SMBIOS generation code
   - [ ] Implement required SMBIOS structures (Types 0, 1, 2, 3, 4, 16, 17, 19, 32)
   - [ ] Export via fw_cfg: `etc/smbios/smbios-tables`
   - [ ] Test: OVMF installs SMBIOS tables

2. **Week 10: Device Configuration**
   - [ ] Verify PCI device compatibility with OVMF
   - [ ] Test Cirrus VGA in PCI mode
   - [ ] Test storage controllers (IDE, AHCI)
   - [ ] Test network devices (e1000, ne2k)
   - [ ] Fix any device enumeration issues

**Deliverable**: OVMF boots to UEFI shell with full hardware detection

### Phase 4: Operating System Boot (Weeks 11-12)

**Goal**: Boot real UEFI operating systems

**Tasks**:

1. **Week 11: Boot Device Support**
   - [ ] Test booting from IDE CD-ROM
   - [ ] Test booting from IDE/SATA hard disk
   - [ ] Test booting from USB mass storage
   - [ ] Implement proper boot order via fw_cfg
   - [ ] Test: Boot UEFI shell from CD

2. **Week 12: OS Installation and Boot**
   - [ ] Test installing Windows 10/11 (UEFI mode)
   - [ ] Test installing Linux (Ubuntu, Fedora) in UEFI mode
   - [ ] Test installing FreeBSD in UEFI mode
   - [ ] Document any compatibility issues
   - [ ] Fix critical boot blockers

**Deliverable**: Successfully boot at least one UEFI OS

### Phase 5: Advanced Features (Weeks 13-16)

**Goal**: Optional enhancements and optimizations

**Tasks**:

1. **Week 13-14: SMM Support (Optional)**
   - [ ] Implement SMRAM memory region
   - [ ] Support SMBASE relocation
   - [ ] Test OVMF with SMM enabled
   - [ ] Note: Required for Secure Boot

2. **Week 15: Q35 Chipset (Optional)**
   - [ ] Implement Q35 northbridge
   - [ ] Implement ICH9 southbridge
   - [ ] Test OVMF with Q35
   - [ ] Compare with i440FX implementation

3. **Week 16: Performance and Polish**
   - [ ] Optimize fw_cfg access performance
   - [ ] Optimize ACPI table generation
   - [ ] Add configuration validation
   - [ ] Update documentation

**Deliverable**: Production-ready UEFI support with optional features

---

## Technical Specifications

### fw_cfg Device Specification

**Register Interface**:

| Port   | Access | Width  | Description                |
|--------|--------|--------|----------------------------|
| 0x510  | Write  | 16-bit | Selector register          |
| 0x511  | R/W    | 8-bit  | Data register              |
| 0x514  | Write  | 32-bit | DMA address (big-endian)   |

**Data Access Pattern**:
1. Write selector to 0x510 (16-bit, little-endian on x86)
2. Read data sequentially from 0x511 (auto-increment offset)
3. Each write to selector resets offset to 0

**File Directory Format**:
```
Offset | Size | Description
-------|------|-------------
0x00   | 4    | File count (big-endian uint32)
0x04   | 64   | First FWCfgFile entry
0x44   | 64   | Second FWCfgFile entry
...
```

**FWCfgFile Structure** (64 bytes):
```c
struct FWCfgFile {
    uint32_t size;        // BE: File size in bytes
    uint16_t select;      // BE: Selector value (>= 0x0020)
    uint16_t reserved;    // Must be 0
    char name[56];        // NUL-terminated ASCII name
};
```

### ACPI Table Loader Specification

**File**: `etc/table-loader`

**Command Format**:
```c
struct RomfileLoaderEntry {
    uint32_t command;
    union {
        struct {
            char file[56];
            uint32_t alignment;
            uint8_t zone;
        } alloc;
        struct {
            char dest_file[56];
            char src_file[56];
            uint32_t offset;
            uint8_t size;
        } add_pointer;
        struct {
            char file[56];
            uint32_t offset;
            uint32_t start;
            uint32_t length;
        } add_checksum;
        struct {
            char dest_file[56];
            char src_file[56];
            uint32_t dst_offset;
            uint32_t src_offset;
            uint8_t size;
        } write_pointer;
    };
};
```

**Commands**:
- `ALLOCATE`: Allocate memory for table file
- `ADD_POINTER`: Add address pointer between tables
- `ADD_CHECKSUM`: Calculate and insert checksum
- `WRITE_POINTER`: Write pointer from one table to another

### Memory Map for UEFI Boot

**Example for 256MB RAM, 2MB OVMF**:
```
0x00000000-0x0009FBFF: Usable RAM (639KB)
0x0009FC00-0x0009FFFF: Reserved (VGA)
0x000A0000-0x000BFFFF: Reserved (VGA framebuffer)
0x000C0000-0x000C7FFF: Reserved (Video BIOS)
0x000C8000-0x000EFFFF: Reserved (Option ROMs)
0x000F0000-0x000FFFFF: Reserved (System BIOS)
0x00100000-0x0FDFFFFF: Usable RAM (253MB)
0x0FE00000-0x0FFFFFFF: Reserved (below firmware)
0x10000000-0xFEDFFFFF: Not present
0xFEE00000-0xFEEFFFFF: Reserved (LAPIC)
0xFEF00000-0xFFDFFFFF: Not present
0xFFE00000-0xFFFFFFFF: Reserved (OVMF firmware - 2MB)
```

### Device Identification

**ACPI Device IDs**:
- fw_cfg: `QEMU0002`
- Chipset: Present as `PNP0A03` (PCI host bridge)

**PCI Device IDs** (i440FX mode):
- Host bridge: 0x8086:0x1237
- ISA bridge: 0x8086:0x7000 (PIIX3)
- IDE controller: 0x8086:0x7010
- ACPI: 0x8086:0x7113 (PIIX4)

---

## Challenges and Risks

### Technical Challenges

1. **Firmware Complexity**
   - OVMF is highly complex with multiple boot phases
   - Debugging firmware is difficult (requires serial port logging)
   - **Mitigation**: Enable OVMF debug builds, use port 0xE9 for debug output

2. **ACPI Table Interdependencies**
   - ACPI tables reference each other via pointers
   - Table loader interface is complex
   - **Mitigation**: Study QEMU's implementation carefully, implement incrementally

3. **Platform-Specific Code**
   - OVMF has platform-specific drivers
   - Expects QEMU-specific behavior in some cases
   - **Mitigation**: Present Bochs as QEMU-compatible via CPUID/SMBIOS

4. **Memory Layout Conflicts**
   - OVMF has specific memory layout requirements
   - Bochs's current ROM mapping may conflict
   - **Mitigation**: Make ROM base address fully configurable

5. **SMM Implementation**
   - System Management Mode is complex
   - Required for Secure Boot
   - **Mitigation**: Implement basic SMM, make Secure Boot optional

### Compatibility Risks

1. **OVMF Version Compatibility**
   - Different OVMF versions may have different requirements
   - **Mitigation**: Target specific OVMF release, document version

2. **Guest OS Compatibility**
   - Some UEFI OSes may have additional requirements
   - **Mitigation**: Test with popular OSes (Windows, Linux, BSD)

3. **Existing Bochs Features**
   - Changes may break legacy BIOS boot
   - **Mitigation**: Keep legacy and UEFI paths separate, extensive regression testing

### Project Risks

1. **Development Complexity**
   - Estimated 16 weeks for full implementation
   - Requires deep understanding of x86 firmware
   - **Mitigation**: Phase implementation, validate each phase

2. **Performance Impact**
   - fw_cfg access, ACPI generation may slow boot
   - **Mitigation**: Profile and optimize critical paths

3. **Maintenance Burden**
   - UEFI code adds significant complexity
   - **Mitigation**: Good documentation, modular design

---

## Testing Strategy

### Unit Testing

**fw_cfg Device Tests**:
```python
# Test selector/data access
write_port(0x510, FW_CFG_SIGNATURE)
signature = read_bytes(0x511, 4)
assert signature == b'QEMU'

# Test file directory
write_port(0x510, FW_CFG_FILE_DIR)
file_count = read_uint32_be(0x511)
assert file_count > 0
```

**ACPI Table Tests**:
```python
# Verify table checksums
for table in acpi_tables:
    assert calculate_checksum(table) == 0

# Verify table signatures
assert rsdp.signature == b'RSD PTR '
assert rsdt.signature == b'RSDT'
assert fadt.signature == b'FACP'
```

### Integration Testing

**Phase 1 Tests**:
- [ ] Load 2MB OVMF.fd at 0xFFE00000
- [ ] Load 4MB OVMF.fd at 0xFFC00000
- [ ] Verify reset vector jumps to OVMF SEC
- [ ] Verify fw_cfg signature read
- [ ] Verify E820 memory map via fw_cfg

**Phase 2 Tests**:
- [ ] OVMF discovers ACPI tables via fw_cfg
- [ ] ACPI tables pass OVMF validation
- [ ] SMP configuration boots with multiple CPUs
- [ ] HPET timer accessible

**Phase 3 Tests**:
- [ ] SMBIOS tables installed by OVMF
- [ ] All PCI devices detected
- [ ] Cirrus VGA functional
- [ ] Storage devices enumerated

**Phase 4 Tests**:
- [ ] Boot to UEFI Shell
- [ ] Load EFI application from FAT partition
- [ ] Install Windows 10/11 in UEFI mode
- [ ] Install Ubuntu in UEFI mode
- [ ] Network boot (PXE) if supported

### Regression Testing

**Legacy BIOS Tests**:
- [ ] SeaBIOS still boots MS-DOS
- [ ] SeaBIOS still boots Windows XP
- [ ] SeaBIOS still boots Linux
- [ ] i440fx.bin still functional

**Performance Tests**:
- [ ] Boot time comparison (legacy vs UEFI)
- [ ] Runtime performance unchanged
- [ ] Memory usage reasonable

### Debugging Tools

**OVMF Debug Builds**:
```bash
# Build OVMF with debug enabled
cd edk2
build -a X64 -t GCC5 -b DEBUG -p OvmfPkg/OvmfPkgX64.dsc
```

**Serial Port Logging**:
```
# .bochsrc configuration
com1: enabled=1, mode=file, dev=ovmf-debug.log
```

**Port 0xE9 Debug Output**:
```cpp
// In Bochs, capture writes to 0xE9
if (port == 0xE9) {
    fprintf(debug_log, "%c", value);
}
```

---

## References

### Bochs Documentation
- Bochs Project: https://bochs.sourceforge.io/
- Bochs Source: https://github.com/bochs-emu/Bochs
- i440fx BIOS: https://github.com/fysnet/i440fx

### UEFI/OVMF Documentation
- UEFI Specification: https://uefi.org/specifications
- EDK2 Project: https://github.com/tianocore/edk2
- OVMF README: https://github.com/tianocore/edk2/blob/master/OvmfPkg/README
- OVMF Whitepaper: https://www.linux-kvm.org/downloads/lersek/ovmf-whitepaper-c770f8c.txt

### QEMU Implementation
- QEMU fw_cfg spec: https://www.qemu.org/docs/master/specs/fw_cfg.html
- QEMU source (hw/nvram/fw_cfg.c): https://github.com/qemu/qemu
- QEMU ACPI generation: https://github.com/qemu/qemu/tree/master/hw/acpi

### Community Resources
- OSDev.org UEFI Wiki: https://wiki.osdev.org/UEFI
- OSDev.org QEMU fw_cfg: https://wiki.osdev.org/QEMU_fw_cfg
- OSDev.org Forum - Bochs UEFI thread: https://forum.osdev.org/viewtopic.php?t=33440

### Related GitHub Issues

#### Open Issues

**[Issue #560](https://github.com/bochs-emu/Bochs/issues/560) - "ACPI is faulty when booting UEFI"** ⚠️ **CRITICAL**
- **Status**: Open
- **Author**: fysnet
- **Created**: June 18, 2025
- **Label**: bios
- **Problem**: ACPI table enumeration fails when booting UEFI firmware on Bochs
  - QEMU: Successfully enumerates 6 ACPI tables (FACP, APIC, HPET, MCFG, WAET, BGRT)
  - Bochs: Only enumerates 2 tables (NULL pointer + BGRT)
  - XSDT length: 52 bytes (Bochs) vs 84 bytes (QEMU)
- **Root Cause**: OVMF debug output shows `OnRootBridgesConnected: InstallAcpiTables: Unsupported`
- **Developer Note** (vruppert): "Qemu generates ACPI data at startup and copies it to BIOS memory. Bochs has no such capability yet and Bochs BIOS does this job in its init code."
- **Impact**: This is THE blocking issue for UEFI support - directly validates our implementation plan's Phase 2 (ACPI Table Generation)

**[Issue #265](https://github.com/bochs-emu/Bochs/issues/265) - "Add TPM 1.2, 2.0 emulation, UEFI with Secure Boot emulation"**
- **Status**: Open (Feature Request)
- **Author**: youself64github
- **Created**: February 9, 2024
- **Labels**: bios, enhancement request
- **Request**: TPM 1.2/2.0 and UEFI with Secure Boot support for Windows 11 compatibility
- **Key Findings**:
  - DrChat confirmed: "Bochs actually already effectively supports UEFI" after bumping `BIOSROMSZ` parameter
  - OVMF firmware loads successfully
  - **Blocker**: OVMF no longer embeds ACPI tables internally - defers to QEMU via pseudo firmware configuration devices
  - Windows bootloaders specifically require MADT (Multiple APIC Description Table)
- **Impact**: Confirms OVMF can load but needs fw_cfg device and ACPI table generation

#### Closed Issues

**[Issue #471](https://github.com/bochs-emu/Bochs/issues/471) - "UEFI boot"**
- **Status**: Closed (Fixed)
- **Author**: fysnet
- **Created**: February 1, 2025
- **Problem**: UEFI ISO hanging during boot when ata1 (secondary ATA controller) was disabled
- **Root Cause**: OVMF sends Execute Device Diagnostic (0x90) command to ata1 and hangs if status register is not 0x00
- **Resolution**: Modified Bochs PCI IDE code to automatically activate both ATA channels when PCI IDE controller is present (matching QEMU behavior)
- **Impact**: Already fixed - no action needed

**[Issue #555](https://github.com/bochs-emu/Bochs/issues/555) - "Panic on CET setting the dirty flag"**
- **Status**: Closed
- **Author**: fysnet
- **Created**: June 3, 2025
- **Label**: cpu
- **Context**: CPU functionality issue during UEFI testing (not directly UEFI-related)
- **Impact**: No direct impact on UEFI implementation

#### Search Results for Other Keywords

- **fw_cfg**: No results found (confirms fw_cfg device doesn't exist in Bochs)
- **EDK2**: No dedicated issues
- **Q35**: No dedicated issues

### Mailing Lists
- Bochs Developer Mailing List: https://sourceforge.net/p/bochs/mailman/bochs-developers/

---

## Appendix A: File Structure

### New Files to Create

```
bochs/iodev/
├── fwcfg.cc              # fw_cfg device implementation
├── fwcfg.h               # fw_cfg device header
├── acpigen.cc            # ACPI table generation
├── acpigen.h             # ACPI generation header
├── smbios.cc             # SMBIOS table generation
└── smbios.h              # SMBIOS header

bochs/docs/
└── uefi-support.md       # User documentation for UEFI

bochs/bios/
└── OVMF.fd               # OVMF firmware binary (2MB or 4MB)
```

### Files to Modify

```
bochs/
├── config.cc             # Add UEFI configuration options
├── .bochsrc              # Add UEFI example configuration
├── memory/
│   ├── memory.h          # Extend ROM size constants
│   ├── memory.cc         # Update memory mapping
│   └── misc_mem.cc       # Configurable ROM base address
├── iodev/
│   ├── devices.cc        # Register fw_cfg device
│   ├── acpi.cc           # Integrate dynamic ACPI tables
│   └── pci.cc            # Ensure UEFI compatibility
└── main.cc               # Initialize UEFI support
```

---

## Appendix B: Configuration Examples

### Legacy BIOS Configuration (existing)
```ini
romimage: file=$BXSHARE/bios/BIOS-bochs-latest, address=0xfffe0000
```

### UEFI Configuration (new)
```ini
# 2MB OVMF firmware
romimage: file=$BXSHARE/bios/OVMF-2MB.fd, address=0xffe00000, type=uefi
firmware: type=uefi
fwcfg: enabled=1
cpu: count=2
memory: guest=256

# 4MB OVMF firmware with SMM
romimage: file=$BXSHARE/bios/OVMF-4MB.fd, address=0xffc00000, type=uefi
firmware: type=uefi, smm=1
fwcfg: enabled=1
cpu: count=4
memory: guest=512
```

---

## Appendix C: Known OVMF Files

**Pre-built OVMF binaries** (from distros):
- Fedora/RHEL: `/usr/share/edk2/ovmf/OVMF_CODE.fd`
- Ubuntu/Debian: `/usr/share/OVMF/OVMF_CODE.fd`
- Arch Linux: `/usr/share/edk2-ovmf/x64/OVMF.fd`

**Build from source**:
```bash
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init
make -C BaseTools
source edksetup.sh
build -a X64 -t GCC5 -p OvmfPkg/OvmfPkgX64.dsc
# Output: Build/OvmfX64/DEBUG_GCC5/FV/OVMF.fd
```

---

## Document Version

- **Version**: 1.1
- **Date**: 2025-11-17
- **Author**: Claude Code (AI Research Assistant)
- **Status**: Complete - Ready for Implementation
- **Last Updated**: 2025-11-17 (Added GitHub issues analysis)

---

## Next Steps

1. ✅ Complete research on Bochs architecture
2. ✅ Complete research on OVMF requirements
3. ✅ Complete research on QEMU implementation
4. ✅ Search GitHub issues in bochs-emu/Bochs repository
   - Found: 2 open issues (#560, #265), 2 closed issues (#471, #555)
   - **Key Finding**: Issue #560 confirms ACPI table generation is THE critical blocker
   - **Key Finding**: Issue #265 confirms OVMF loads but needs fw_cfg + ACPI tables
   - **Key Finding**: No fw_cfg, EDK2, or Q35 issues exist (confirms these are not yet implemented)
5. ✅ Update this document with findings from issue search
6. ⏳ Review and refine implementation plan based on findings
7. ⏳ **READY**: Begin Phase 1 implementation (fw_cfg device + memory layout)
8. ⏳ Address Issue #560 during Phase 2 (ACPI table generation)

### Key Validation from GitHub Issues

The issue search **validates our implementation plan**:

✅ **Issue #560** confirms Phase 2 (ACPI Table Generation) is critical - OVMF cannot install ACPI tables
✅ **Issue #265** confirms OVMF can load with BIOSROMSZ changes but needs fw_cfg device
✅ **No fw_cfg issues** confirms we're building this from scratch (Phase 1)
✅ **Issue #471** shows PCI IDE compatibility is already fixed

**Conclusion**: Our 16-week, 5-phase implementation plan directly addresses all known issues. Phase 1 and Phase 2 will resolve the two blocking issues preventing UEFI boot.

---

*This is a living document and will be updated as implementation progresses.*
