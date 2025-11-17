# PCI IDE Controller Investigation for UEFI/OVMF Boot

## Problem Statement
OVMF firmware loads successfully in Bochs but does not boot from the IDE disk, despite the same disk booting successfully in QEMU with OVMF.

## Root Cause Analysis

Investigated PCI IDE controller configuration by comparing Bochs implementation with QEMU's working PIIX3 IDE controller. Found three critical configuration issues:

### Issue 1: Missing PCI BARs 0-3
**Problem**: Bochs PCI IDE only configured BAR4 (Bus Master IDE), leaving BAR0-BAR3 unset (zero).

**Impact**: UEFI firmware scans PCI BARs to discover device I/O ports. Without BAR0-BAR3, OVMF cannot determine where the IDE controller's command and control registers are located.

**QEMU Configuration**:
- BAR0 = 0x1F1 (Primary Command Block: 0x1F0 + I/O flag)
- BAR1 = 0x3F5 (Primary Control Block: 0x3F4 + I/O flag)
- BAR2 = 0x171 (Secondary Command Block: 0x170 + I/O flag)
- BAR3 = 0x375 (Secondary Control Block: 0x374 + I/O flag)

**Fix**: Added BAR0-BAR3 initialization in `pci_ide.cc::reset()` (lines 139-163)

### Issue 2: Missing Interrupt Pin Configuration  
**Problem**: PCI IDE controller initialized with interrupt pin = 0 (no interrupt).

**Impact**: UEFI expects PCI devices to have proper interrupt configuration. Without an interrupt pin, the device may appear incomplete or non-functional to the firmware.

**QEMU Configuration**: Uses INTA (interrupt pin = 1)

**Fix**: Changed all three chipsets (I430FX, I440BX, I440FX) to use `BX_PCI_INTA` in `pci_ide.cc::init()` (lines 107-112)

### Issue 3: PCI BAR Write Blocking
**Problem**: `pci_write_handler()` blocked ALL writes to addresses 0x10-0x1F (BAR0-BAR3).

**Impact**: During PCI enumeration, UEFI writes 0xFFFFFFFF to each BAR to determine its size, then writes the allocated address. Bochs was silently ignoring these writes, making the BARs appear non-functional or non-existent.

**Fix**: Removed BAR0-BAR3 from write blocking in `pci_ide.cc::pci_write_handler()` (line 463-466)

## Fixes Applied

All fixes committed in commit `1e7058e`:

```cpp
// bochs/iodev/pci_ide.cc

// Fix 1: BAR0-BAR3 configuration (in reset())
BX_PIDE_THIS pci_conf[0x10] = 0xF1;  // BAR0: 0x1F0 | 0x01
BX_PIDE_THIS pci_conf[0x11] = 0x01;
// ... (full BAR0-BAR3 setup)

// Fix 2: Interrupt pin (in init())
init_pci_conf(0x8086, 0x7010, 0x00, 0x010180, 0x00, BX_PCI_INTA);

// Fix 3: Allow BAR writes (in pci_write_handler())
if ((address > 0x23) && (address < 0x40))  // Only block 0x24-0x3F
    return;
```

## Current Status

**Fixes Implemented**: ✓ Complete
- BAR0-BAR3 configured with legacy IDE I/O ports
- Interrupt pin set to INTA for all chipsets
- BAR write blocking removed for proper enumeration

**Boot Status**: ✗ Still not working
- OVMF loads and runs
- No disk I/O activity observed
- Black screen persists (no boot menu, no errors shown)

## Next Investigation Steps

The PCI configuration is now correct and matches QEMU, but OVMF still doesn't boot. Possible causes:

### 1. OVMF IDE Driver Support
- Modern OVMF builds may have limited or no IDE/PATA support
- OVMF primarily supports AHCI (SATA) and NVMe
- Need to verify if the OVMF_CODE.fd binary includes IDE driver

**Action**: Check OVMF build configuration and driver inclusion

### 2. PCI Device Enumeration
- OVMF might not be discovering PCI function 1 (IDE controller)
- Or device might be discovered but driver not loading

**Action**: Add debug logging to see if OVMF reads PCI config space

### 3. Boot Device Path  
- Current fw_cfg bootorder: `/pci@i0cf8/ide@1,1/drive@0/disk@0`
- This is QEMU's OpenFirmware path notation
- Bochs PCI implementation might need different path

**Action**: Verify PCI device address and path format

### 4. Alternative Approach: AHCI
- OVMF has better AHCI support than IDE
- Consider implementing AHCI controller instead
- Would require significant development but more future-proof

**Action**: Research AHCI implementation complexity

## Testing Configuration

**Disk Image**: `uefi-gpt-disk.img`
- 100MB GPT-partitioned disk
- EFI System Partition (FAT32)
- Contains UEFI Shell at /EFI/BOOT/BOOTX64.EFI
- **Verified working** in QEMU with same OVMF firmware

**Firmware**: `OVMF_CODE.fd`
- RELEASE build (no debug output available)
- Same binary works in QEMU

**Verification Command**:
```bash
qemu-system-x86_64 -bios bochs/bios/OVMF_CODE.fd -hda uefi-gpt-disk.img -m 256
# ↑ Successfully boots to UEFI Shell
```

## Files Modified

1. `bochs/iodev/pci_ide.cc` - PCI IDE controller implementation
   - Lines 107-112: Interrupt pin configuration
   - Lines 139-163: BAR0-BAR3 initialization
   - Lines 463-466: BAR write handling

## Conclusion

We've identified and fixed three critical PCI configuration bugs that prevented UEFI firmware from properly enumerating the IDE controller. The configuration now matches QEMU's working implementation.

However, OVMF still doesn't boot from the IDE disk, suggesting the problem may be:
1. Missing IDE driver in OVMF build
2. PCI enumeration issues beyond configuration
3. Need for AHCI instead of IDE

**Recommendation**: Investigate OVMF IDE driver support and consider AHCI implementation as alternative path forward.
