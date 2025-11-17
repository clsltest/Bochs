# Phase 3: ACPI Table Implementation Plan

## Overview

ACPI tables are the final critical component needed for OVMF to complete boot.
Based on analysis of Bochs BIOS code (rombios32.c), we need to generate and
expose the following tables via fw_cfg.

## Required ACPI Tables

### 1. RSDP (Root System Description Pointer) - 20 bytes
- Signature: "RSD PTR " (split to avoid detection in strings)
- OEM ID: "BOCHS " (or "QEMU " for compatibility)
- Checksum: Calculated over 20 bytes
- RSDT address: 32-bit physical address

**fw_cfg exposure**: `etc/acpi/rsdp` (20 bytes)

### 2. RSDT (Root System Description Table)
- Header: Standard ACPI table header
- Entry pointers: Array of 32-bit addresses pointing to:
  - FADT
  - MADT
  - SSDT
  - HPET (if enabled)

### 3. FADT (Fixed ACPI Description Table)
- Firmware Control: Points to FACS
- DSDT: Points to DSDT table
- Model: 1 (PC/AT compatible)
- SCI interrupt: PM interrupt number
- SMI command: 0xB2 (SMI_CMD_IO_ADDR)
- Power management register addresses
- Feature flags

### 4. FACS (Firmware ACPI Control Structure)
- Signature: "FACS"
- Firmware waking vector
- Global lock

### 5. DSDT (Differentiated System Description Table)
- AML bytecode describing system devices
- **Already exists**: `bochs/bios/acpi-dsdt.hex` (3640 bytes)
- Contains device definitions, PCI routing, etc.

### 6. MADT (Multiple APIC Description Table)
- Local APIC address: 0xFEE00000
- Per-CPU entries (APIC_PROCESSOR type)
- I/O APIC entry (0xFEC00000)
- Interrupt overrides (IRQ 0 → GSI 2)

### 7. SSDT (Secondary System Description Table) - Optional
- Additional AML code
- Currently used in Bochs for runtime-generated devices

### 8. HPET (High Precision Event Timer) - If enabled
- Timer block ID: 0x8086A201
- Address: 0xFED00000

## Implementation Strategy

### Approach: Port from Bochs BIOS

**Pros**:
- Known working tables
- Already tested with legacy BIOS boot
- Includes all necessary devices

**Cons**:
- Need to adapt from BIOS context to fw_cfg context
- BIOS uses physical memory addresses, fw_cfg uses buffers

### Code Structure

```cpp
// In fwcfg.h
struct acpi_table_rsdp { ... };
struct acpi_table_header { ... };
struct acpi_table_rsdt { ... };
struct acpi_table_fadt { ... };
struct acpi_table_facs { ... };
struct acpi_table_madt { ... };

// In fwcfg.cc
void generate_acpi_tables();
void build_rsdp(...);
void build_rsdt(...);
void build_fadt(...);
void build_madt(...);
uint8_t acpi_checksum(void *data, int len);
```

### Memory Layout for ACPI Tables

QEMU/fw_cfg approach:
```
etc/acpi/rsdp    - 20 bytes (RSDP structure)
etc/acpi/tables  - All other tables concatenated:
                   RSDT (36+ bytes)
                   FADT (116 bytes)
                   FACS (64 bytes)
                   DSDT (3640 bytes from acpi-dsdt.hex)
                   MADT (variable, ~80 bytes for 1 CPU)
                   HPET (56 bytes if enabled)
```

### fw_cfg Table Pointers

Since all tables are in a contiguous buffer, we use offsets:
```
RSDP.rsdt_physical_address → Not used by OVMF (reads from fw_cfg)
RSDT.entry[0] → Offset to FADT within etc/acpi/tables
RSDT.entry[1] → Offset to MADT within etc/acpi/tables
FADT.firmware_ctrl → Offset to FACS within etc/acpi/tables
FADT.dsdt → Offset to DSDT within etc/acpi/tables
```

## Implementation Steps

### Step 1: Add ACPI Structures to fwcfg.h
- Define all ACPI table structures
- Use packed structures for correct layout
- Add constants for addresses and sizes

### Step 2: Implement ACPI Table Generation in fwcfg.cc
- `generate_acpi_tables()` - Main generation function
- `acpi_checksum()` - Calculate ACPI checksums
- Individual table builders for each table type

### Step 3: Integrate into fw_cfg Initialization
- Call generate_acpi_tables() from init()
- Add two fw_cfg files:
  - `etc/acpi/rsdp`
  - `etc/acpi/tables`

### Step 4: Testing
- Verify OVMF probes fw_cfg for ACPI
- Check for "ACPI" messages in Bochs output
- Monitor OVMF boot progression

## Expected Results

### Before ACPI Tables:
- OVMF stalls after ACPI controller init
- No progress beyond PM base address setup
- Possibly waiting in a poll loop

### After ACPI Tables:
- OVMF should discover and install ACPI tables
- Boot process should progress significantly
- Possible messages about UEFI services initialization
- May reach point where it looks for boot devices

## Estimated Effort

- **Code**: ~400-500 lines
  - Structures: ~150 lines
  - Generation: ~250-350 lines
- **Testing**: Moderate complexity
- **Debug**: May need to adjust table offsets/checksums

## Reference Implementation

Source: `/home/user/Bochs/bochs/bios/rombios32.c`
Lines: 1452-1970 (ACPI table generation)

Key functions:
- `acpi_bios_init()` - Main initialization
- `acpi_build_table_header()` - Build standard header
- `acpi_checksum()` - Calculate checksums

## Next Actions

1. Create ACPI structure definitions in fwcfg.h
2. Implement table generation in fwcfg.cc
3. Test with OVMF and verify boot progression
4. Debug any table format issues
5. Document results and commit

