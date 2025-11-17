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
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////

// QEMU fw_cfg device - Firmware Configuration Interface
// Provides platform configuration data to firmware (OVMF/UEFI)

#define BX_PLUGGABLE

#include "iodev.h"

#if BX_SUPPORT_PCI

#include "fwcfg.h"
#include <vector>

#define LOG_THIS theFwCfg->

bx_fwcfg_c *theFwCfg = NULL;

// Plugin entry point
extern "C" {
PLUGIN_ENTRY_FOR_MODULE(fwcfg)
{
  if (mode == PLUGIN_INIT) {
    theFwCfg = new bx_fwcfg_c();
    BX_REGISTER_DEVICE_DEVMODEL(plugin, type, theFwCfg, BX_PLUGIN_FWCFG);
  } else if (mode == PLUGIN_FINI) {
    delete theFwCfg;
  } else if (mode == PLUGIN_PROBE) {
    return (int)PLUGTYPE_OPTIONAL;
  }
  return 0; // Success
}
}

// Constructor
bx_fwcfg_c::bx_fwcfg_c()
{
  put("FWCFG");
  memset(&s, 0, sizeof(s));
}

// Destructor
bx_fwcfg_c::~bx_fwcfg_c()
{
  cleanup_file_directory();

  // Free file data
  for (size_t i = 0; i < s.files.size(); i++) {
    if (s.files[i].data != NULL) {
      delete[] s.files[i].data;
    }
  }
  s.files.clear();

  SIM->get_bochs_root()->remove("fwcfg");
  BX_DEBUG(("Exit"));
}

// Initialize device
void bx_fwcfg_c::init(void)
{
  BX_INFO(("QEMU fw_cfg device"));

  // Register I/O ports
  DEV_register_iowrite_handler(this, write_handler, FW_CFG_PORT_SEL, "fw_cfg selector", 3);
  DEV_register_ioread_handler(this, read_handler, FW_CFG_PORT_DATA, "fw_cfg data", 1);
  DEV_register_iowrite_handler(this, write_handler, FW_CFG_PORT_DATA, "fw_cfg data", 1);

  // Initialize signature
  s.signature[0] = 'Q';
  s.signature[1] = 'E';
  s.signature[2] = 'M';
  s.signature[3] = 'U';

  // Interface version (3 = supports files)
  s.interface_version = 0x00000003;

  // Get RAM size from memory subsystem
  s.ram_size = (Bit64u)SIM->get_param_num(BXPN_MEM_SIZE)->get() * BX_CONST64(1024) * BX_CONST64(1024);

  // Get CPU count
  s.nb_cpus = (Bit16u)SIM->get_param_num(BXPN_CPU_NPROCESSORS)->get();
  s.max_cpus = (Bit16u)SIM->get_param_num(BXPN_CPU_NCORES)->get() *
               (Bit16u)SIM->get_param_num(BXPN_CPU_NTHREADS)->get() *
               s.nb_cpus;
  if (s.max_cpus == 0) s.max_cpus = s.nb_cpus;

  // Machine ID (1 = PC)
  s.machine_id = 1;

  // Graphics mode
  s.nographic = 0; // Assume graphics unless nogui

  // Generate random UUID
  for (int i = 0; i < 16; i++) {
    s.uuid[i] = (Bit8u)(rand() & 0xFF);
  }
  // Set UUID version 4 (random)
  s.uuid[6] = (s.uuid[6] & 0x0F) | 0x40;
  s.uuid[8] = (s.uuid[8] & 0x3F) | 0x80;

  // Initialize file directory
  s.next_file_selector = 0x0020; // Files start at selector 32
  s.file_dir_data = NULL;
  s.file_dir_size = 0;

  // Generate E820 memory map
  generate_e820_map();

  BX_INFO(("fw_cfg initialized: RAM=%llu MB, CPUs=%u/%u",
           (unsigned long long)(s.ram_size / (1024*1024)), s.nb_cpus, s.max_cpus));
}

// Reset device
void bx_fwcfg_c::reset(unsigned type)
{
  s.cur_selector = 0;
  s.cur_offset = 0;
}

// Register state for save/restore
void bx_fwcfg_c::register_state(void)
{
  bx_list_c *list = new bx_list_c(SIM->get_bochs_root(), "fwcfg", "QEMU fw_cfg State");
  BXRS_HEX_PARAM_FIELD(list, cur_selector, s.cur_selector);
  BXRS_HEX_PARAM_FIELD(list, cur_offset, s.cur_offset);
}

// I/O write handler
void bx_fwcfg_c::write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len)
{
  bx_fwcfg_c *class_ptr = (bx_fwcfg_c *)this_ptr;
  class_ptr->write(address, value, io_len);
}

void bx_fwcfg_c::write(Bit32u address, Bit32u value, unsigned io_len)
{
  if (address == FW_CFG_PORT_SEL) {
    // Selector register write (16-bit little-endian)
    Bit16u selector = (Bit16u)(value & 0xFFFF);
    select_item(selector);
    BX_DEBUG(("write selector: 0x%04x", selector));
  } else if (address == FW_CFG_PORT_DATA) {
    // Data register write (8-bit)
    write_byte((Bit8u)(value & 0xFF));
  }
}

// I/O read handler
Bit32u bx_fwcfg_c::read_handler(void *this_ptr, Bit32u address, unsigned io_len)
{
  bx_fwcfg_c *class_ptr = (bx_fwcfg_c *)this_ptr;
  return class_ptr->read(address, io_len);
}

Bit32u bx_fwcfg_c::read(Bit32u address, unsigned io_len)
{
  if (address == FW_CFG_PORT_DATA) {
    // Data register read (8-bit)
    return read_byte();
  }
  return 0xFF;
}

// Select item and reset offset
void bx_fwcfg_c::select_item(Bit16u selector)
{
  s.cur_selector = selector;
  s.cur_offset = 0;
}

// Read one byte from currently selected item
Bit8u bx_fwcfg_c::read_byte()
{
  Bit8u value = 0xFF;
  Bit32u offset = s.cur_offset++;

  // Handle built-in selectors
  if (s.cur_selector < 0x0020) {
    switch (s.cur_selector) {
      case FW_CFG_SIGNATURE:
        if (offset < 4) {
          value = s.signature[offset];
        }
        break;

      case FW_CFG_ID:
        if (offset < 4) {
          value = (s.interface_version >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_UUID:
        if (offset < 16) {
          value = s.uuid[offset];
        }
        break;

      case FW_CFG_RAM_SIZE:
        if (offset < 8) {
          value = (s.ram_size >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_NOGRAPHIC:
        if (offset < 2) {
          value = (s.nographic >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_NB_CPUS:
        if (offset < 2) {
          value = (s.nb_cpus >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_MAX_CPUS:
        if (offset < 2) {
          value = (s.max_cpus >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_MACHINE_ID:
        if (offset < 2) {
          value = (s.machine_id >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_FILE_DIR:
        // Generate file directory on demand
        if (s.file_dir_data == NULL) {
          generate_file_directory();
        }
        if (offset < s.file_dir_size) {
          value = s.file_dir_data[offset];
        }
        break;

      default:
        // Unknown selector
        if (offset == 0) {
          BX_DEBUG(("read from unknown selector 0x%04x", s.cur_selector));
        }
        value = 0xFF;
        break;
    }
  } else {
    // Handle file-based selectors
    int file_idx = find_file_by_selector(s.cur_selector);
    if (file_idx >= 0) {
      if (offset < s.files[file_idx].size) {
        value = s.files[file_idx].data[offset];
      }
    } else {
      if (offset == 0) {
        BX_DEBUG(("read from unknown file selector 0x%04x", s.cur_selector));
      }
      value = 0xFF;
    }
  }

  return value;
}

// Write one byte to currently selected item
void bx_fwcfg_c::write_byte(Bit8u value)
{
  Bit32u offset = s.cur_offset++;

  // Currently no writable items implemented
  // Future: bootorder file will be writable
  BX_DEBUG(("write to selector 0x%04x offset 0x%x: 0x%02x (ignored)",
            s.cur_selector, offset, value));
}

// Add a file-based item
void bx_fwcfg_c::add_file(const char *name, Bit8u *data, Bit32u size, bool writable)
{
  if (strlen(name) >= 56) {
    BX_ERROR(("fw_cfg: filename too long: %s", name));
    return;
  }

  FWCfgEntry entry;
  entry.selector = s.next_file_selector++;
  strncpy(entry.name, name, 55);
  entry.name[55] = '\0';
  entry.data = data;
  entry.size = size;
  entry.writable = writable;

  s.files.push_back(entry);

  // Invalidate cached file directory
  cleanup_file_directory();

  BX_INFO(("fw_cfg: added file '%s' (selector=0x%04x, size=%u)",
           name, entry.selector, size));
}

// Find file by selector
int bx_fwcfg_c::find_file_by_selector(Bit16u selector)
{
  for (size_t i = 0; i < s.files.size(); i++) {
    if (s.files[i].selector == selector) {
      return (int)i;
    }
  }
  return -1;
}

// Generate file directory (selector 0x0019)
void bx_fwcfg_c::generate_file_directory()
{
  cleanup_file_directory();

  Bit32u num_files = (Bit32u)s.files.size();
  s.file_dir_size = 4 + (num_files * 64); // 4-byte count + 64 bytes per file
  s.file_dir_data = new Bit8u[s.file_dir_size];

  // Write file count (big-endian)
  s.file_dir_data[0] = (num_files >> 24) & 0xFF;
  s.file_dir_data[1] = (num_files >> 16) & 0xFF;
  s.file_dir_data[2] = (num_files >> 8) & 0xFF;
  s.file_dir_data[3] = num_files & 0xFF;

  // Write file entries
  for (Bit32u i = 0; i < num_files; i++) {
    Bit8u *entry = &s.file_dir_data[4 + (i * 64)];
    FWCfgEntry *file = &s.files[i];

    // File size (big-endian)
    entry[0] = (file->size >> 24) & 0xFF;
    entry[1] = (file->size >> 16) & 0xFF;
    entry[2] = (file->size >> 8) & 0xFF;
    entry[3] = file->size & 0xFF;

    // Selector (big-endian)
    entry[4] = (file->selector >> 8) & 0xFF;
    entry[5] = file->selector & 0xFF;

    // Reserved
    entry[6] = 0;
    entry[7] = 0;

    // Name (NUL-padded)
    memset(&entry[8], 0, 56);
    strncpy((char *)&entry[8], file->name, 55);
  }

  BX_DEBUG(("generated file directory: %u files, %u bytes", num_files, s.file_dir_size));
}

// Clean up file directory cache
void bx_fwcfg_c::cleanup_file_directory()
{
  if (s.file_dir_data != NULL) {
    delete[] s.file_dir_data;
    s.file_dir_data = NULL;
    s.file_dir_size = 0;
  }
}

// Generate E820 memory map and add to fw_cfg
void bx_fwcfg_c::generate_e820_map()
{
  std::vector<E820Entry> entries;
  E820Entry entry;

  // Get memory size in bytes
  Bit64u ram_size_bytes = s.ram_size;
  Bit64u ram_size_mb = ram_size_bytes / (1024 * 1024);

  BX_INFO(("Generating E820 memory map for %llu MB RAM", (unsigned long long)ram_size_mb));

  // Entry 1: Low memory (0 - 640KB)
  // This is always usable RAM
  entry.address = 0x00000000;
  entry.length = 0x000A0000;  // 640KB
  entry.type = E820_RAM;
  entries.push_back(entry);
  BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu KB) - Usable RAM",
           (unsigned long long)entry.address,
           (unsigned long long)(entry.address + entry.length - 1),
           (unsigned long long)(entry.length / 1024)));

  // Entry 2: VGA/BIOS area (640KB - 1MB)
  // Reserved for video memory and option ROMs
  entry.address = 0x000A0000;
  entry.length = 0x00060000;  // 384KB (640KB to 1MB)
  entry.type = E820_RESERVED;
  entries.push_back(entry);
  BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu KB) - Reserved (VGA/BIOS)",
           (unsigned long long)entry.address,
           (unsigned long long)(entry.address + entry.length - 1),
           (unsigned long long)(entry.length / 1024)));

  // Entry 3: High memory (1MB to end of RAM or 3.5GB, whichever is lower)
  // For systems with >3.5GB RAM, we need to split around the UEFI ROM area
  Bit64u high_mem_start = 0x00100000;  // 1MB
  Bit64u high_mem_end = ram_size_bytes;
  Bit64u uefi_rom_start = 0xFFC00000;  // 4GB - 4MB

  if (high_mem_end > uefi_rom_start) {
    // RAM extends into UEFI ROM area, split it
    entry.address = high_mem_start;
    entry.length = uefi_rom_start - high_mem_start;
    entry.type = E820_RAM;
    entries.push_back(entry);
    BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu MB) - Usable RAM (below UEFI)",
             (unsigned long long)entry.address,
             (unsigned long long)(entry.address + entry.length - 1),
             (unsigned long long)(entry.length / (1024*1024))));
  } else {
    // RAM doesn't reach UEFI ROM area, single entry
    entry.address = high_mem_start;
    entry.length = high_mem_end - high_mem_start;
    entry.type = E820_RAM;
    entries.push_back(entry);
    BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu MB) - Usable RAM",
             (unsigned long long)entry.address,
             (unsigned long long)(entry.address + entry.length - 1),
             (unsigned long long)(entry.length / (1024*1024))));
  }

  // Entry 4: UEFI ROM area (0xFFC00000 - 0xFFFFFFFF = 4MB)
  // This is where OVMF firmware is loaded
  entry.address = 0xFFC00000;
  entry.length = 0x00400000;  // 4MB
  entry.type = E820_RESERVED;
  entries.push_back(entry);
  BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu MB) - Reserved (UEFI ROM)",
           (unsigned long long)entry.address,
           (unsigned long long)(entry.address + entry.length - 1),
           (unsigned long long)(entry.length / (1024*1024))));

  // Allocate memory for E820 data
  Bit32u e820_size = entries.size() * sizeof(E820Entry);
  Bit8u *e820_data = new Bit8u[e820_size];

  // Copy entries to data buffer (already in little-endian format on x86)
  memcpy(e820_data, entries.data(), e820_size);

  // Add E820 map as a file "etc/e820"
  add_file("etc/e820", e820_data, e820_size, false);

  BX_INFO(("E820 memory map: %u entries, %u bytes", (unsigned)entries.size(), e820_size));
}

#endif // BX_SUPPORT_PCI
