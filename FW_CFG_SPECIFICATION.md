# QEMU fw_cfg Device Specification for Bochs

## Document Information

- **Version**: 1.0
- **Date**: 2025-11-17
- **Author**: Claude Code (AI Research Assistant)
- **Status**: Implementation Ready
- **Purpose**: Specification for implementing QEMU fw_cfg device in Bochs for UEFI/OVMF support

---

## Table of Contents

1. [Overview](#overview)
2. [Hardware Interface](#hardware-interface)
3. [Selector Values](#selector-values)
4. [File-Based Interface](#file-based-interface)
5. [Data Structures](#data-structures)
6. [Access Protocol](#access-protocol)
7. [Implementation Plan](#implementation-plan)
8. [Testing Strategy](#testing-strategy)
9. [References](#references)

---

## Overview

### What is fw_cfg?

The **fw_cfg device** (QEMU Firmware Configuration device) is a simple interface that allows virtual machine firmware (like OVMF/UEFI) to retrieve configuration data and platform information from the emulator/hypervisor. It was originally developed for QEMU but has become the de facto standard for passing data to firmware in virtual machines.

### Why Does OVMF Need It?

Modern OVMF firmware (EDK2-based UEFI) **requires** fw_cfg to:

1. **Detect virtualization**: Verify it's running in a virtual machine
2. **Get ACPI tables**: Retrieve dynamically-generated ACPI tables (DSDT, FADT, MADT, etc.)
3. **Get memory map**: Obtain E820 memory map for the guest OS
4. **Get SMP information**: Learn about multiple CPUs/cores
5. **Get boot configuration**: Determine boot order and options
6. **Get SMBIOS tables**: System management BIOS information

**Without fw_cfg**, OVMF will stall during initialization (as we observed in Phase 1 testing).

### Device Identification

- **ACPI ID**: `QEMU0002`
- **I/O Ports**: 0x510 (selector), 0x511 (data), 0x514 (DMA)
- **Detection**: Check CPUID leaf 0x40000000 or ACPI tables for presence

---

## Hardware Interface

### I/O Port Map

| Port   | Name                    | Access | Width  | Description                          |
|--------|-------------------------|--------|--------|--------------------------------------|
| 0x510  | FW_CFG_PORT_SEL         | Write  | 16-bit | Selector register (which item?)      |
| 0x511  | FW_CFG_PORT_DATA        | R/W    | 8-bit  | Data register (read/write bytes)     |
| 0x514  | FW_CFG_PORT_DMA         | Write  | 32-bit | DMA address register (optional)      |

**Note**: For initial implementation, we'll implement only 0x510 and 0x511. DMA support (0x514) can be added later.

### Register Behavior

#### Selector Register (0x510)

```
Bits:    15-0
Type:    Write-only
Endian:  Little-endian on x86
Purpose: Select which configuration item to access
```

**When written**:
- Sets the current selector value
- Resets the data offset to 0
- Prepares for sequential reads from data register

**Special values**:
- `0x0000-0x0018`: Architecture-independent items (signature, RAM size, etc.)
- `0x0019`: File directory (for file-based interface)
- `0x0020+`: File-based items (assigned dynamically)

#### Data Register (0x511)

```
Bits:    7-0
Type:    Read/Write
Endian:  N/A (byte access only)
Purpose: Read or write data for currently selected item
```

**When read**:
- Returns next byte from current selector's data
- Automatically increments internal offset by 1
- Wraps or returns 0xFF when past end of data

**When written** (for writable items):
- Writes byte to current selector's data
- Automatically increments internal offset by 1
- Used for bootorder and similar writable items

---

## Selector Values

### Architecture-Independent Selectors (0x0000-0x0018)

| Selector | Name                    | Size    | Type     | Description                          |
|----------|-------------------------|---------|----------|--------------------------------------|
| 0x0000   | FW_CFG_SIGNATURE        | 4 bytes | Read     | Returns "QEMU" (0x51454D55)         |
| 0x0001   | FW_CFG_ID               | 4 bytes | Read     | Interface version (0x00000003)      |
| 0x0002   | FW_CFG_UUID             | 16 bytes| Read     | VM UUID                             |
| 0x0003   | FW_CFG_RAM_SIZE         | 8 bytes | Read     | RAM size in bytes (little-endian)   |
| 0x0004   | FW_CFG_NOGRAPHIC        | 2 bytes | Read     | Graphics mode (0=graphic, 1=nogui)  |
| 0x0005   | FW_CFG_NB_CPUS          | 2 bytes | Read     | Number of CPUs (little-endian)      |
| 0x0006   | FW_CFG_MACHINE_ID       | 2 bytes | Read     | Machine ID (PC=1)                   |
| 0x0007   | FW_CFG_KERNEL_ADDR      | 8 bytes | Read     | Kernel load address (if direct boot)|
| 0x0008   | FW_CFG_KERNEL_SIZE      | 4 bytes | Read     | Kernel size                         |
| 0x0009   | FW_CFG_KERNEL_CMDLINE   | Variable| Read     | Kernel command line string          |
| 0x000A   | FW_CFG_INITRD_ADDR      | 8 bytes | Read     | Initrd address                      |
| 0x000B   | FW_CFG_INITRD_SIZE      | 4 bytes | Read     | Initrd size                         |
| 0x000C   | FW_CFG_BOOT_DEVICE      | 2 bytes | Read     | Boot device selector                |
| 0x000D   | FW_CFG_NUMA             | Variable| Read     | NUMA configuration                  |
| 0x000E   | FW_CFG_BOOT_MENU        | Variable| Read     | Boot menu wait time                 |
| 0x000F   | FW_CFG_MAX_CPUS         | 2 bytes | Read     | Maximum CPUs supported              |
| 0x0010   | FW_CFG_KERNEL_ENTRY     | 8 bytes | Read     | Kernel entry point                  |
| 0x0011   | FW_CFG_KERNEL_DATA      | Variable| Read     | Kernel data blob                    |
| 0x0012   | FW_CFG_INITRD_DATA      | Variable| Read     | Initrd data blob                    |
| 0x0013   | FW_CFG_CMDLINE_ADDR     | 8 bytes | Read     | Command line address                |
| 0x0014   | FW_CFG_CMDLINE_SIZE     | 4 bytes | Read     | Command line size                   |
| 0x0015   | FW_CFG_CMDLINE_DATA     | Variable| Read     | Command line data                   |
| 0x0016   | FW_CFG_SETUP_ADDR       | 8 bytes | Read     | Setup data address                  |
| 0x0017   | FW_CFG_SETUP_SIZE       | 4 bytes | Read     | Setup data size                     |
| 0x0018   | FW_CFG_SETUP_DATA       | Variable| Read     | Setup data                          |
| 0x0019   | FW_CFG_FILE_DIR         | Variable| Read     | File directory (see below)          |

### Priority Selectors for OVMF Boot

For initial OVMF boot, implement these **in order of priority**:

**Phase 1 (Critical - Required for OVMF initialization)**:
1. **0x0000** - FW_CFG_SIGNATURE: Must return "QEMU" for OVMF to detect fw_cfg
2. **0x0001** - FW_CFG_ID: Version identifier
3. **0x0003** - FW_CFG_RAM_SIZE: Total RAM in bytes
4. **0x0005** - FW_CFG_NB_CPUS: Number of CPUs (at least 1)
5. **0x0019** - FW_CFG_FILE_DIR: File directory for file-based items

**Phase 2 (High - Required for ACPI/Boot)**:
6. **File: etc/e820**: E820 memory map
7. **File: etc/acpi/rsdp**: ACPI RSDP pointer
8. **File: etc/acpi/tables**: ACPI tables blob
9. **File: etc/table-loader**: ACPI table loader commands

**Phase 3 (Medium - Enhances boot)**:
10. **0x0002** - FW_CFG_UUID: VM unique identifier
11. **File: etc/smbios/smbios-tables**: SMBIOS tables
12. **File: bootorder**: Boot device order

---

## File-Based Interface

### Overview

Starting with selector **0x0019** (FW_CFG_FILE_DIR), fw_cfg provides a file-based interface where named items can be accessed. This is how OVMF retrieves ACPI tables, E820 maps, and other complex data.

### File Directory Structure

When selector **0x0019** is written to 0x510, reading from 0x511 returns:

```
Offset  | Size     | Description
--------|----------|----------------------------------------
0x00    | 4 bytes  | File count (N) - big-endian uint32
0x04    | 64 bytes | First FWCfgFile structure
0x44    | 64 bytes | Second FWCfgFile structure
...     | ...      | (N total FWCfgFile structures)
```

### FWCfgFile Structure (64 bytes)

```c
struct FWCfgFile {
    uint32_t size;        // File size in bytes (big-endian)
    uint16_t select;      // Selector value for this file (big-endian)
    uint16_t reserved;    // Must be 0
    char name[56];        // NUL-terminated ASCII filename
};
```

**Example FWCfgFile** (etc/e820):
```
Offset  | Value                    | Description
--------|--------------------------|---------------------------
0x00    | 0x00000078              | Size: 120 bytes (BE)
0x04    | 0x0020                  | Selector: 32 (BE)
0x06    | 0x0000                  | Reserved
0x08    | "etc/e820\0..."         | Name (NUL-padded to 56)
```

### Standard File Names

**Required for OVMF**:
- `etc/e820` - E820 memory map
- `etc/acpi/rsdp` - ACPI Root System Description Pointer
- `etc/acpi/tables` - ACPI tables data
- `etc/table-loader` - ACPI table loader commands

**Optional but recommended**:
- `etc/smbios/smbios-tables` - SMBIOS data
- `bootorder` - Boot device order
- `etc/boot-cpus` - SMP CPU count
- `etc/smp-layout` - SMP topology
- `opt/ovmf/PcdResizeXfb` - Display resolution

### File Selector Allocation

Files are assigned selectors **starting from 0x0020** (32) in the order they are registered:

```
0x0020: First file (e.g., etc/e820)
0x0021: Second file (e.g., etc/acpi/rsdp)
0x0022: Third file (e.g., etc/acpi/tables)
...
```

---

## Data Structures

### C++ Structures for Bochs Implementation

```cpp
// fw_cfg selector values
#define FW_CFG_SIGNATURE        0x0000
#define FW_CFG_ID               0x0001
#define FW_CFG_UUID             0x0002
#define FW_CFG_RAM_SIZE         0x0003
#define FW_CFG_NOGRAPHIC        0x0004
#define FW_CFG_NB_CPUS          0x0005
#define FW_CFG_MACHINE_ID       0x0006
#define FW_CFG_FILE_DIR         0x0019

// I/O port addresses
#define FW_CFG_PORT_SEL         0x0510
#define FW_CFG_PORT_DATA        0x0511
#define FW_CFG_PORT_DMA         0x0514

// File directory entry
struct FWCfgFile {
    Bit32u size;          // Big-endian
    Bit16u select;        // Big-endian
    Bit16u reserved;
    char name[56];
};

// Internal file entry
struct FWCfgEntry {
    Bit16u selector;
    char name[56];
    Bit8u *data;
    Bit32u size;
    bool writable;
};

// fw_cfg device state
class bx_fwcfg_c : public bx_devmodel_c {
private:
    struct {
        Bit16u cur_selector;      // Currently selected item
        Bit32u cur_offset;        // Current read/write offset

        // File-based interface
        std::vector<FWCfgEntry> files;
        Bit16u next_file_selector;  // Next available selector (starts at 0x20)

        // Static data storage for basic selectors
        Bit8u signature[4];       // "QEMU"
        Bit32u interface_version;
        Bit8u uuid[16];
        Bit64u ram_size;
        Bit16u nographic;
        Bit16u nb_cpus;
        Bit16u max_cpus;
        Bit16u machine_id;

    } s;

public:
    bx_fwcfg_c();
    virtual ~bx_fwcfg_c();
    virtual void init(void);
    virtual void reset(unsigned type);

    // I/O port handlers
    static Bit32u read_handler(void *this_ptr, Bit32u address, unsigned io_len);
    static void write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len);

    // API for adding file-based items
    void add_file(const char *name, Bit8u *data, Bit32u size, bool writable = false);
    void add_bytes(Bit16u selector, Bit8u *data, Bit32u size);

private:
    Bit8u read_byte();
    void write_byte(Bit8u value);
    void select_item(Bit16u selector);
    void generate_file_directory(Bit8u **data_out, Bit32u *size_out);
};
```

---

## Access Protocol

### Reading Data from fw_cfg

**Standard Read Sequence**:

```python
# Example: Read RAM size (8 bytes)
1. Write selector to port 0x510
   out_word(0x510, FW_CFG_RAM_SIZE)  # 0x0003

2. Read data sequentially from port 0x511
   byte0 = in_byte(0x511)  # Offset 0 -> auto-increment to 1
   byte1 = in_byte(0x511)  # Offset 1 -> auto-increment to 2
   byte2 = in_byte(0x511)  # Offset 2 -> auto-increment to 3
   byte3 = in_byte(0x511)  # Offset 3 -> auto-increment to 4
   byte4 = in_byte(0x511)  # Offset 4 -> auto-increment to 5
   byte5 = in_byte(0x511)  # Offset 5 -> auto-increment to 6
   byte6 = in_byte(0x511)  # Offset 6 -> auto-increment to 7
   byte7 = in_byte(0x511)  # Offset 7 -> end of data

   # Combine bytes (little-endian for most values)
   ram_size = byte0 | (byte1 << 8) | (byte2 << 16) | ...
```

**Reading File Directory**:

```python
1. Select file directory
   out_word(0x510, FW_CFG_FILE_DIR)  # 0x0019

2. Read file count (4 bytes, big-endian)
   count_b0 = in_byte(0x511)  # MSB
   count_b1 = in_byte(0x511)
   count_b2 = in_byte(0x511)
   count_b3 = in_byte(0x511)  # LSB
   count = (count_b0 << 24) | (count_b1 << 16) | (count_b2 << 8) | count_b3

3. For each file, read 64-byte FWCfgFile structure
   for i in range(count):
       entry = read_bytes_from_port_0x511(64)
       # Parse: size (4B BE), selector (2B BE), reserved (2B), name (56B)
       file_size = be32(entry[0:4])
       file_selector = be16(entry[4:6])
       file_name = cstring(entry[8:64])
```

**Reading a File**:

```python
# Example: Read etc/e820
1. Get selector from file directory (e.g., 0x0020)

2. Select the file
   out_word(0x510, file_selector)  # e.g., 0x0020

3. Read file data
   data = []
   for i in range(file_size):
       data.append(in_byte(0x511))
```

### Important Notes

1. **Offset Auto-Increment**: Each read from 0x511 automatically increments internal offset
2. **Selector Resets Offset**: Writing to 0x510 resets offset to 0
3. **Endianness**:
   - Selector values: Little-endian (x86 native)
   - File directory counts: **Big-endian**
   - File directory fields: **Big-endian**
   - Data content: Varies by item (usually little-endian for integers)

---

## Implementation Plan

### Phase 1: Basic fw_cfg Device (Week 1)

**Goal**: Implement minimal fw_cfg device that OVMF can detect

**Tasks**:
1. Create `bochs/iodev/fwcfg.cc` and `bochs/iodev/fwcfg.h`
2. Register I/O ports 0x510 and 0x511
3. Implement selector storage and offset tracking
4. Implement these selectors:
   - `FW_CFG_SIGNATURE` → "QEMU"
   - `FW_CFG_ID` → 0x00000003
   - `FW_CFG_RAM_SIZE` → Get from BX_MEM_THIS
   - `FW_CFG_NB_CPUS` → Get from SIM config

**Success Criteria**:
- OVMF detects fw_cfg device (no more "fw_cfg not found" errors)
- Can read signature "QEMU" from port 0x511

### Phase 2: File-Based Interface (Week 1-2)

**Goal**: Implement file directory and file reading

**Tasks**:
1. Implement `FW_CFG_FILE_DIR` selector
2. Create dynamic file registration API
3. Implement file directory generation
4. Add helper methods: `add_file()`, `generate_file_directory()`
5. Test with dummy file (e.g., "test/hello")

**Success Criteria**:
- Can read file count from directory
- Can enumerate files in directory
- Can read file contents via selector

### Phase 3: E820 Memory Map (Week 2)

**Goal**: Provide E820 memory map via fw_cfg

**Tasks**:
1. Generate E820 memory map from Bochs memory configuration
2. Add `etc/e820` file with proper format
3. E820 entry structure:
   ```c
   struct e820_entry {
       uint64_t addr;    // Base address
       uint64_t size;    // Region size
       uint32_t type;    // Type (1=usable, 2=reserved, etc.)
   };
   ```

**E820 Map Layout** (for 256MB RAM):
```
0x00000000-0x0009FBFF: Type 1 (Usable RAM) - 639KB
0x0009FC00-0x0009FFFF: Type 2 (Reserved) - 1KB
0x000A0000-0x000BFFFF: Type 2 (Reserved) - VGA 128KB
0x000C0000-0x000FFFFF: Type 2 (Reserved) - BIOS 256KB
0x00100000-0x0FFFFFFF: Type 1 (Usable RAM) - ~255MB
0xFEC00000-0xFEC00FFF: Type 2 (Reserved) - IOAPIC 4KB
0xFEE00000-0xFEE00FFF: Type 2 (Reserved) - LAPIC 4KB
0xFFC00000-0xFFFFFFFF: Type 2 (Reserved) - Firmware 4MB
```

**Success Criteria**:
- OVMF can read E820 map
- OVMF's memory map matches Bochs configuration

### Phase 4: ACPI Tables Stub (Week 2-3)

**Goal**: Provide minimal ACPI tables via fw_cfg

**Tasks**:
1. Create minimal ACPI tables:
   - RSDP (Root System Description Pointer)
   - RSDT (Root System Description Table)
   - FADT (Fixed ACPI Description Table)
   - DSDT (Differentiated System Description Table)
   - MADT (Multiple APIC Description Table) - for SMP
2. Add files:
   - `etc/acpi/rsdp` → RSDP structure
   - `etc/acpi/tables` → Concatenated tables
3. Initially use static/hardcoded tables, dynamic generation in Phase 2 (later)

**Success Criteria**:
- OVMF finds ACPI tables
- OVMF enumerates at least FADT and MADT
- No more "InstallAcpiTables: Unsupported" error

### Phase 5: SMBIOS Tables (Week 3)

**Goal**: Provide system information via SMBIOS

**Tasks**:
1. Generate SMBIOS tables (Type 0, 1, 3, 4, 16, 17, 19, 32)
2. Add `etc/smbios/smbios-tables` file
3. Populate with Bochs system information

**Success Criteria**:
- OVMF installs SMBIOS tables
- Guest OS can query system info

---

## Testing Strategy

### Unit Tests

**Test 1: Signature Verification**
```python
def test_signature():
    write_port(0x510, 0x0000)  # FW_CFG_SIGNATURE
    sig = ""
    for i in range(4):
        sig += chr(read_port(0x511))
    assert sig == "QEMU"
```

**Test 2: Offset Auto-Increment**
```python
def test_offset_increment():
    write_port(0x510, FW_CFG_RAM_SIZE)  # Select RAM size
    byte0 = read_port(0x511)
    byte1 = read_port(0x511)
    # Verify different bytes read (offset incremented)
    assert byte0 != byte1 or both are valid
```

**Test 3: Selector Reset**
```python
def test_selector_reset():
    write_port(0x510, FW_CFG_RAM_SIZE)
    first_byte = read_port(0x511)
    # Re-select same item
    write_port(0x510, FW_CFG_RAM_SIZE)
    second_byte = read_port(0x511)
    # Should read same first byte
    assert first_byte == second_byte
```

**Test 4: File Directory**
```python
def test_file_directory():
    write_port(0x510, FW_CFG_FILE_DIR)
    count_bytes = [read_port(0x511) for _ in range(4)]
    count = (count_bytes[0] << 24) | (count_bytes[1] << 16) | \
            (count_bytes[2] << 8) | count_bytes[3]
    assert count > 0
    # Read first file entry (64 bytes)
    entry = [read_port(0x511) for _ in range(64)]
    file_size = (entry[0] << 24) | (entry[1] << 16) | \
                (entry[2] << 8) | entry[3]
    file_selector = (entry[4] << 8) | entry[5]
    assert file_selector >= 0x0020  # Files start at 32
```

### Integration Tests

**Test 5: OVMF Detection**
```bash
# Run Bochs with OVMF
./bochs -q -f test-uefi.bochsrc

# Check debug log for fw_cfg detection
grep "fw_cfg" ovmf-debug.log
# Should see: "QemuFwCfgProbe: firmware config interface detected"
```

**Test 6: E820 Map Retrieval**
```
# In OVMF debug log, look for:
"E820: Entry[0]: Base=0x0000000000000000 Length=0x000000000009FC00 Type=1"
"E820: Entry[1]: Base=0x0000000000100000 Length=0x000000000FF00000 Type=1"
```

**Test 7: ACPI Table Installation**
```
# OVMF should log:
"ProcessCmdAllocate: Allocated 0x00XXXXXX for ACPI tables"
"InstallAcpiTables: installed X tables"
```

### Regression Tests

**Test 8: Legacy BIOS Still Works**
```bash
# Boot with SeaBIOS instead of OVMF
romimage: file=bios/BIOS-bochs-latest
# Verify no fw_cfg interference with legacy boot
```

---

## Error Handling

### Invalid Selector

**Behavior**: When an unknown/invalid selector is written to 0x510:
- Store the selector anyway
- Reads from 0x511 return 0xFF
- Log warning in Bochs debug output

```cpp
Bit8u bx_fwcfg_c::read_byte() {
    if (s.cur_selector < 0x0020) {
        // Handle built-in selectors
        switch (s.cur_selector) {
            case FW_CFG_SIGNATURE: ...
            case FW_CFG_ID: ...
            // ...
            default:
                BX_ERROR(("FW_CFG: read from unknown selector 0x%04x", s.cur_selector));
                return 0xFF;
        }
    } else {
        // Handle file-based selectors
        int file_idx = find_file_by_selector(s.cur_selector);
        if (file_idx < 0) {
            BX_ERROR(("FW_CFG: read from unknown file selector 0x%04x", s.cur_selector));
            return 0xFF;
        }
        // ...
    }
}
```

### Out of Bounds Read

**Behavior**: When reading past end of data:
- Return 0xFF for all reads beyond data size
- Do not wrap around
- Log warning if excessive reads detected

```cpp
if (s.cur_offset >= data_size) {
    if (s.cur_offset == data_size) {
        BX_DEBUG(("FW_CFG: read past end of selector 0x%04x", s.cur_selector));
    }
    return 0xFF;
}
```

---

## Performance Considerations

### Optimization Strategies

1. **Cache File Directory**: Generate file directory once, reuse for all reads
2. **Lazy Data Generation**: Only generate data (like ACPI tables) when first accessed
3. **Minimize Allocations**: Use stack buffers for small items, heap only for large files
4. **Fast Selector Lookup**: Use switch statement for built-ins, hash map for files

### Memory Usage

Estimated memory usage:
- Device state structure: ~256 bytes
- File directory cache: ~10 files × 64 bytes = 640 bytes
- File data storage:
  - E820 map: ~200 bytes
  - ACPI tables: ~4-8 KB
  - SMBIOS tables: ~1-2 KB
  - Total: **~10-15 KB**

This is negligible compared to emulator memory usage.

---

## Debugging Support

### Debug Logging Levels

```cpp
#define BX_FWCFG_DEBUG 1

#if BX_FWCFG_DEBUG
  #define FWCFG_LOG(level, ...) BX_##level(("FW_CFG: " __VA_ARGS__))
#else
  #define FWCFG_LOG(level, ...)
#endif
```

**Log Messages**:
- **INFO**: Device init, file registration
- **DEBUG**: Selector changes, data reads
- **ERROR**: Invalid selectors, protocol violations

### Port 0xE9 Debug Output

OVMF can write debug messages to port 0xE9. Bochs should capture these:

```cpp
// In iodev/biosdev.cc or unmapped.cc
if (address == 0xE9) {
    fprintf(debug_file, "%c", value);
    fflush(debug_file);
}
```

This helps diagnose OVMF's fw_cfg usage.

---

## References

### QEMU Documentation
- [QEMU fw_cfg Specification](https://www.qemu.org/docs/master/specs/fw_cfg.html)
- [QEMU source: hw/nvram/fw_cfg.c](https://github.com/qemu/qemu/blob/master/hw/nvram/fw_cfg.c)

### OVMF/EDK2 Documentation
- [OVMF README](https://github.com/tianocore/edk2/blob/master/OvmfPkg/README)
- [QemuFwCfgLib](https://github.com/tianocore/edk2/tree/master/OvmfPkg/Library/QemuFwCfgLib)
- [AcpiPlatformDxe](https://github.com/tianocore/edk2/tree/master/OvmfPkg/AcpiPlatformDxe)

### Community Resources
- [OSDev Wiki: QEMU fw_cfg](https://wiki.osdev.org/QEMU_fw_cfg)
- [Bochs Issue #560](https://github.com/bochs-emu/Bochs/issues/560)
- [Bochs Issue #265](https://github.com/bochs-emu/Bochs/issues/265)

### Standards
- [ACPI Specification](https://uefi.org/specifications)
- [SMBIOS Specification](https://www.dmtf.org/standards/smbios)
- [UEFI Specification](https://uefi.org/specifications)

---

## Appendix A: Complete Selector Reference

| Hex    | Dec | Name                    | Size     | Priority | Notes                    |
|--------|-----|-------------------------|----------|----------|--------------------------|
| 0x0000 | 0   | SIGNATURE               | 4        | Critical | "QEMU" magic bytes       |
| 0x0001 | 1   | ID                      | 4        | Critical | Version = 3              |
| 0x0002 | 2   | UUID                    | 16       | Low      | VM identifier            |
| 0x0003 | 3   | RAM_SIZE                | 8        | Critical | Total RAM in bytes       |
| 0x0004 | 4   | NOGRAPHIC               | 2        | Low      | Display mode             |
| 0x0005 | 5   | NB_CPUS                 | 2        | High     | CPU count                |
| 0x0006 | 6   | MACHINE_ID              | 2        | Medium   | Machine type (PC=1)      |
| 0x0007 | 7   | KERNEL_ADDR             | 8        | Low      | Direct kernel boot       |
| 0x0008 | 8   | KERNEL_SIZE             | 4        | Low      | Direct kernel boot       |
| 0x0009 | 9   | KERNEL_CMDLINE          | Var      | Low      | Direct kernel boot       |
| 0x000A | 10  | INITRD_ADDR             | 8        | Low      | Direct kernel boot       |
| 0x000B | 11  | INITRD_SIZE             | 4        | Low      | Direct kernel boot       |
| 0x000C | 12  | BOOT_DEVICE             | 2        | Low      | Legacy boot device       |
| 0x000D | 13  | NUMA                    | Var      | Low      | NUMA topology            |
| 0x000E | 14  | BOOT_MENU               | Var      | Low      | Boot menu config         |
| 0x000F | 15  | MAX_CPUS                | 2        | Medium   | Max CPU count            |
| 0x0010 | 16  | KERNEL_ENTRY            | 8        | Low      | Direct kernel boot       |
| 0x0011 | 17  | KERNEL_DATA             | Var      | Low      | Direct kernel boot       |
| 0x0012 | 18  | INITRD_DATA             | Var      | Low      | Direct kernel boot       |
| 0x0013 | 19  | CMDLINE_ADDR            | 8        | Low      | Direct kernel boot       |
| 0x0014 | 20  | CMDLINE_SIZE            | 4        | Low      | Direct kernel boot       |
| 0x0015 | 21  | CMDLINE_DATA            | Var      | Low      | Direct kernel boot       |
| 0x0016 | 22  | SETUP_ADDR              | 8        | Low      | Direct kernel boot       |
| 0x0017 | 23  | SETUP_SIZE              | 4        | Low      | Direct kernel boot       |
| 0x0018 | 24  | SETUP_DATA              | Var      | Low      | Direct kernel boot       |
| 0x0019 | 25  | FILE_DIR                | Var      | Critical | File directory           |

---

## Appendix B: E820 Memory Type Constants

```c
#define E820_RAM         1  // Usable RAM
#define E820_RESERVED    2  // Reserved by system
#define E820_ACPI        3  // ACPI reclaimable memory
#define E820_NVS         4  // ACPI NVS memory
#define E820_UNUSABLE    5  // Unusable memory (bad RAM)
```

---

## Appendix C: Implementation Checklist

### Phase 1: Device Infrastructure
- [ ] Create fwcfg.h header file
- [ ] Create fwcfg.cc implementation file
- [ ] Define FW_CFG constants
- [ ] Create bx_fwcfg_c class
- [ ] Register as Bochs plugin
- [ ] Register I/O port handlers
- [ ] Implement init() method
- [ ] Implement reset() method
- [ ] Add device to build system (Makefile)

### Phase 2: Basic Selectors
- [ ] Implement selector write handler (port 0x510)
- [ ] Implement data read handler (port 0x511)
- [ ] Add offset tracking
- [ ] Implement FW_CFG_SIGNATURE
- [ ] Implement FW_CFG_ID
- [ ] Implement FW_CFG_RAM_SIZE
- [ ] Implement FW_CFG_NB_CPUS
- [ ] Implement FW_CFG_MAX_CPUS
- [ ] Implement FW_CFG_MACHINE_ID
- [ ] Test with OVMF

### Phase 3: File Interface
- [ ] Design file storage structure
- [ ] Implement add_file() method
- [ ] Implement file lookup by selector
- [ ] Implement FW_CFG_FILE_DIR generation
- [ ] Handle big-endian conversion
- [ ] Test file enumeration

### Phase 4: E820 Map
- [ ] Generate E820 entries from memory config
- [ ] Format E820 data structure
- [ ] Register etc/e820 file
- [ ] Test with OVMF
- [ ] Verify memory map correctness

### Phase 5: ACPI Tables
- [ ] Generate minimal RSDP
- [ ] Generate minimal RSDT
- [ ] Generate minimal FADT
- [ ] Generate minimal DSDT
- [ ] Generate MADT (for SMP)
- [ ] Register etc/acpi/rsdp file
- [ ] Register etc/acpi/tables file
- [ ] Test ACPI table loading in OVMF

### Phase 6: Testing & Polish
- [ ] Add comprehensive logging
- [ ] Add error handling
- [ ] Write unit tests
- [ ] Test with multiple CPU counts
- [ ] Test with different RAM sizes
- [ ] Test OVMF boot to shell
- [ ] Test legacy BIOS compatibility
- [ ] Update documentation
- [ ] Create usage guide

---

*End of fw_cfg Specification*
