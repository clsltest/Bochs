# OVMF fw_cfg DMA Hang - Root Cause and Fix

## Problem

OVMF firmware would hang during early boot (PEI phase) when trying to read the fw_cfg file directory (selector 0x19).

### Symptoms
- OVMF serial debug output showed:
  ```
  QemuFwCfgProbe: Supported 1, DMA 1
  Select Item: 0x19     ← HANGS HERE
  ```
- System appeared frozen
- Never reached DXE phase or PCI enumeration
- PCI IDE fixes from previous work were never tested

## Root Cause Analysis

### Investigation Process

1. **Built OVMF with DEBUG support and serial output**
   - Used `-D DEBUG_ON_SERIAL_PORT` flag in EDK2 build
   - This allowed capturing detailed boot logs via COM1

2. **Added debug logging to Bochs fw_cfg device** (bochs/iodev/fwcfg.cc)
   - Enhanced file directory read logging
   - Added file listing during directory generation

3. **Analyzed Bochs debug logs**
   - Found that after `write selector: 0x0019`, there were writes to ports 0x514 and 0x518
   - These ports are **fw_cfg DMA control registers**
   - Writes went to UNMAP (unmapped I/O ports)
   - OVMF was waiting for DMA operation to complete, which never happened

### The Bug

**File**: `bochs/iodev/fwcfg.cc:100`

```cpp
// BEFORE (BROKEN):
s.interface_version = 0x00000003;  // Advertised DMA support!
```

**Interface version bits:**
- Bit 0 (0x1): File support
- Bit 1 (0x2): DMA support
- Value 0x3: Both files AND DMA

**Problem**: Bochs advertised DMA support (interface_version = 0x3) but had **NO DMA implementation**!
- No I/O port handlers registered for 0x514/0x518 (DMA control registers)
- OVMF detected DMA support and tried to use it for file directory read
- DMA operation never completed → infinite hang

## The Fix

**File**: `bochs/iodev/fwcfg.cc:100`

```cpp
// AFTER (FIXED):
// Interface version (1 = supports files, 3 = files + DMA)
// Note: DMA is not implemented, so we only advertise file support
s.interface_version = 0x00000001;  // Files only, no DMA
```

**Simple one-line change**: Changed interface_version from 0x3 to 0x1
- OVMF now sees fw_cfg without DMA support
- Falls back to standard I/O port reads (port 0x511)
- File directory reads complete successfully

## Results

### Before Fix
- OVMF hung at file directory read
- Log: 35 lines (1.9K)
- Never reached DXE phase

### After Fix
- OVMF progresses through PEI phase ✓
- OVMF reaches DXE phase ✓
- Log: 909 lines (22K+) in 2 minutes
- PCI bus driver (PciBusDxe) loads ✓
- Multiple UEFI drivers load successfully ✓
- PCI IDE controller detected by Bochs ✓

### Bochs Debug Log After Fix
```
00000000000i[DEV   ] PIIX3 PCI IDE controller present at device 1, function 1
00000000000i[HD    ] HD on ata0-0: 'uefi-gpt-disk.img', 'flat' mode
00000000000i[HD    ] ata0-0: autodetect geometry: CHS=203/16/63 (sector size=512)
```

## Additional Debug Enhancements

While investigating, also added better fw_cfg debug logging (kept for future debugging):

**bochs/iodev/fwcfg.cc:261-278** - Enhanced file directory read logging
**bochs/iodev/fwcfg.cc:392-398** - Log file directory contents when generated

## Related Work

This fix unblocks the PCI IDE controller fixes made previously:
- BAR0-BAR3 configuration (bochs/iodev/pci_ide.cc:139-163)
- Interrupt pin configuration (bochs/iodev/pci_ide.cc:107-112)
- BAR write handling (bochs/iodev/pci_ide.cc:463-466)

Those fixes are now actually reachable since OVMF can boot past PEI phase.

## Testing

### Test Configuration
- OVMF DEBUG build with serial output (`-D DEBUG_ON_SERIAL_PORT`)
- Bochs configured with COM1 serial logging
- Test duration: 120 seconds (timeout)
- GPT disk image: `uefi-gpt-disk.img`

### Test Results
- ✓ fw_cfg file directory reads complete
- ✓ DXE phase reached
- ✓ Multiple UEFI drivers loaded
- ✓ PCI bus enumeration occurs
- ✓ IDE controller detected

## Next Steps

To achieve full UEFI boot:
1. Verify OVMF loads SATA/IDE drivers (AtaBusDxe, SataController)
2. Confirm IDE controller is enumerated by OVMF PCI bus
3. Test if OVMF can read boot files from GPT disk
4. Debug any remaining boot issues

## Files Changed

1. **bochs/iodev/fwcfg.cc** (line 100)
   - Changed `s.interface_version = 0x00000003` → `0x00000001`
   - Added debug logging for file directory (lines 261-278, 392-398)

2. **test-uefi-serial-debug.bochsrc** (enhanced)
   - Added Bochs debug logging configuration

## Commit Message

```
Fix OVMF fw_cfg DMA hang by disabling unimplemented DMA support

OVMF was hanging during early boot when reading fw_cfg file directory
(selector 0x19). Investigation revealed:

1. Bochs advertised DMA support (interface_version = 0x3) but had no
   DMA implementation
2. OVMF detected DMA and tried to use it (wrote to ports 0x514/0x518)
3. These writes went to unmapped I/O ports (no handlers registered)
4. OVMF hung waiting for DMA operation to complete

Fix: Change interface_version from 0x3 to 0x1 (files only, no DMA)
Result: OVMF now successfully reads file directory and continues boot

After this fix:
- OVMF progresses from 35 lines (1.9K) to 909+ lines (22K+)
- Reaches DXE phase and loads UEFI drivers
- PCI bus enumeration occurs
- IDE controller is detected

Also added enhanced debug logging to fw_cfg for future debugging.
```
