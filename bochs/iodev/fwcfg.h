/////////////////////////////////////////////////////////////////////////
// $Id$
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2025  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA B 02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////

#ifndef BX_IODEV_FWCFG_H
#define BX_IODEV_FWCFG_H

#include <vector>

// QEMU fw_cfg device - Firmware Configuration Interface
// This device provides a simple interface for firmware (OVMF/UEFI) to
// retrieve platform configuration data from the emulator.

// Selector values for architecture-independent items
#define FW_CFG_SIGNATURE        0x0000  // Returns "QEMU"
#define FW_CFG_ID               0x0001  // Interface version
#define FW_CFG_UUID             0x0002  // VM UUID
#define FW_CFG_RAM_SIZE         0x0003  // RAM size in bytes
#define FW_CFG_NOGRAPHIC        0x0004  // Graphics mode
#define FW_CFG_NB_CPUS          0x0005  // Number of CPUs
#define FW_CFG_MACHINE_ID       0x0006  // Machine ID
#define FW_CFG_KERNEL_ADDR      0x0007  // Kernel address (direct boot)
#define FW_CFG_KERNEL_SIZE      0x0008  // Kernel size
#define FW_CFG_KERNEL_CMDLINE   0x0009  // Kernel command line
#define FW_CFG_INITRD_ADDR      0x000A  // Initrd address
#define FW_CFG_INITRD_SIZE      0x000B  // Initrd size
#define FW_CFG_BOOT_DEVICE      0x000C  // Boot device
#define FW_CFG_NUMA             0x000D  // NUMA data
#define FW_CFG_BOOT_MENU        0x000E  // Boot menu
#define FW_CFG_MAX_CPUS         0x000F  // Maximum CPUs
#define FW_CFG_KERNEL_ENTRY     0x0010  // Kernel entry point
#define FW_CFG_KERNEL_DATA      0x0011  // Kernel data
#define FW_CFG_INITRD_DATA      0x0012  // Initrd data
#define FW_CFG_CMDLINE_ADDR     0x0013  // Command line address
#define FW_CFG_CMDLINE_SIZE     0x0014  // Command line size
#define FW_CFG_CMDLINE_DATA     0x0015  // Command line data
#define FW_CFG_SETUP_ADDR       0x0016  // Setup address
#define FW_CFG_SETUP_SIZE       0x0017  // Setup size
#define FW_CFG_SETUP_DATA       0x0018  // Setup data
#define FW_CFG_FILE_DIR         0x0019  // File directory

// I/O port addresses
#define FW_CFG_PORT_SEL         0x0510  // Selector register (16-bit write)
#define FW_CFG_PORT_DATA        0x0511  // Data register (8-bit read/write)
#define FW_CFG_PORT_DMA         0x0514  // DMA address (32-bit write) - not implemented yet

// File directory entry (64 bytes)
struct FWCfgFile {
    Bit32u size;          // File size (big-endian)
    Bit16u select;        // Selector value (big-endian)
    Bit16u reserved;      // Reserved (must be 0)
    char name[56];        // NUL-terminated filename
};

#if BX_SUPPORT_PCI

// Internal file entry
struct FWCfgEntry {
    Bit16u selector;
    char name[56];
    Bit8u *data;
    Bit32u size;
    bool writable;
};

class bx_fwcfg_c : public bx_devmodel_c {
public:
    bx_fwcfg_c();
    virtual ~bx_fwcfg_c();
    virtual void init(void);
    virtual void reset(unsigned type);
    virtual void register_state(void);

    // I/O port handlers
    static Bit32u read_handler(void *this_ptr, Bit32u address, unsigned io_len);
    static void write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len);

    // API for adding file-based items
    void add_file(const char *name, Bit8u *data, Bit32u size, bool writable = false);

private:
    struct {
        Bit16u cur_selector;      // Currently selected item
        Bit32u cur_offset;        // Current read/write offset within selected item

        // File-based interface
        std::vector<FWCfgEntry> files;
        Bit16u next_file_selector;  // Next available selector (starts at 0x20)

        // Cached file directory (generated on demand)
        Bit8u *file_dir_data;
        Bit32u file_dir_size;

        // Static data for basic selectors
        Bit8u signature[4];       // "QEMU"
        Bit32u interface_version;  // Version identifier
        Bit8u uuid[16];           // VM UUID
        Bit64u ram_size;          // RAM size in bytes
        Bit16u nographic;         // Graphics mode (0=graphic, 1=nogui)
        Bit16u nb_cpus;           // Number of CPUs
        Bit16u max_cpus;          // Maximum CPUs
        Bit16u machine_id;        // Machine type (1=PC)

    } s;

    // Internal methods
    Bit32u read(Bit32u address, unsigned io_len);
    void write(Bit32u address, Bit32u value, unsigned io_len);
    Bit8u read_byte();
    void write_byte(Bit8u value);
    void select_item(Bit16u selector);
    void generate_file_directory();
    int find_file_by_selector(Bit16u selector);
    void cleanup_file_directory();
};

#endif // BX_SUPPORT_PCI

#endif // BX_IODEV_FWCFG_H
