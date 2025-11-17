#!/usr/bin/env python3
"""
Create a minimal FAT12 bootable disk image for UEFI testing.
This creates a simple FAT12 filesystem that OVMF can boot from.
"""

import struct
import os
import sys

def create_fat12_boot_disk(image_path, efi_file_path, size_mb=50):
    """Create a FAT12 disk image with UEFI boot file"""

    size_bytes = size_mb * 1024 * 1024
    sector_size = 512
    sectors_per_cluster = 1
    reserved_sectors = 1
    num_fats = 2
    root_entries = 512

    total_sectors = size_bytes // sector_size

    # Calculate FAT12 parameters
    root_dir_sectors = (root_entries * 32 + sector_size - 1) // sector_size

    # Estimate sectors per FAT (for FAT12, this is usually small)
    # FAT12 can address 4084 clusters max
    max_clusters = min(4084, (total_sectors - reserved_sectors - root_dir_sectors) // sectors_per_cluster)
    fat_size_bytes = ((max_clusters + 2) * 12 + 7) // 8  # 12 bits per entry
    sectors_per_fat = (fat_size_bytes + sector_size - 1) // sector_size

    # Calculate layout
    fat1_start = reserved_sectors
    fat2_start = fat1_start + sectors_per_fat
    root_dir_start = fat2_start + sectors_per_fat
    data_start = root_dir_start + root_dir_sectors

    print(f"Creating {size_mb}MB FAT12 disk image...")
    print(f"  Total sectors: {total_sectors}")
    print(f"  Sectors per FAT: {sectors_per_fat}")
    print(f"  Root directory sectors: {root_dir_sectors}")
    print(f"  Data starts at sector: {data_start}")

    # Create empty image
    with open(image_path, 'wb') as f:
        f.write(b'\x00' * size_bytes)

    # Write boot sector
    with open(image_path, 'r+b') as f:
        # FAT12 Boot Sector
        boot_sector = bytearray(512)

        # Jump instruction
        boot_sector[0:3] = b'\xEB\x3C\x90'

        # OEM Name
        boot_sector[3:11] = b'MSWIN4.1'

        # BIOS Parameter Block (BPB)
        struct.pack_into('<H', boot_sector, 11, sector_size)           # Bytes per sector
        struct.pack_into('<B', boot_sector, 13, sectors_per_cluster)    # Sectors per cluster
        struct.pack_into('<H', boot_sector, 14, reserved_sectors)       # Reserved sectors
        struct.pack_into('<B', boot_sector, 16, num_fats)               # Number of FATs
        struct.pack_into('<H', boot_sector, 17, root_entries)           # Root entries

        # Total sectors (use 16-bit if < 65536, otherwise use 32-bit field)
        if total_sectors < 65536:
            struct.pack_into('<H', boot_sector, 19, total_sectors)
            struct.pack_into('<I', boot_sector, 32, 0)
        else:
            struct.pack_into('<H', boot_sector, 19, 0)
            struct.pack_into('<I', boot_sector, 32, total_sectors)

        boot_sector[21] = 0xF8                                          # Media descriptor (hard disk)
        struct.pack_into('<H', boot_sector, 22, sectors_per_fat)        # Sectors per FAT
        struct.pack_into('<H', boot_sector, 24, 63)                     # Sectors per track
        struct.pack_into('<H', boot_sector, 26, 255)                    # Number of heads
        struct.pack_into('<I', boot_sector, 28, 0)                      # Hidden sectors

        # Extended BPB
        boot_sector[36] = 0x29                                          # Extended boot signature
        struct.pack_into('<I', boot_sector, 37, 0x12345678)             # Volume serial number
        boot_sector[41:52] = b'UEFI_TEST  '                             # Volume label
        boot_sector[54:62] = b'FAT12   '                                # Filesystem type

        # Boot signature
        boot_sector[510:512] = b'\x55\xAA'

        f.seek(0)
        f.write(boot_sector)

        # Initialize FAT tables
        fat = bytearray(sectors_per_fat * sector_size)
        # FAT12 first two entries (media descriptor)
        fat[0:3] = b'\xF8\xFF\xFF'

        # Write both FAT copies
        f.seek(fat1_start * sector_size)
        f.write(fat)
        f.seek(fat2_start * sector_size)
        f.write(fat)

        # Initialize root directory
        f.seek(root_dir_start * sector_size)
        f.write(b'\x00' * (root_dir_sectors * sector_size))

    print(f"✓ FAT12 filesystem created")

    # Now add the EFI directory structure and boot file
    # We'll manually create directory entries and write the file

    if not os.path.exists(efi_file_path):
        print(f"Error: EFI file not found: {efi_file_path}")
        return False

    with open(efi_file_path, 'rb') as ef:
        efi_data = ef.read()

    efi_size = len(efi_data)
    efi_clusters_needed = (efi_size + (sectors_per_cluster * sector_size) - 1) // (sectors_per_cluster * sector_size)

    print(f"  EFI file size: {efi_size} bytes ({efi_clusters_needed} clusters)")

    with open(image_path, 'r+b') as f:
        # Create root directory entries
        f.seek(root_dir_start * sector_size)

        # Entry 1: EFI directory
        entry = bytearray(32)
        entry[0:11] = b'EFI        '  # Directory name (8.3 format, padded)
        entry[11] = 0x10               # Attribute: Directory
        first_cluster_efi_dir = 2      # First cluster of EFI directory
        struct.pack_into('<H', entry, 26, first_cluster_efi_dir)
        f.write(entry)

        # Write EFI directory cluster
        efi_dir_offset = data_start * sector_size + (first_cluster_efi_dir - 2) * sectors_per_cluster * sector_size
        f.seek(efi_dir_offset)

        # Entry in EFI dir: BOOT subdirectory
        entry = bytearray(32)
        entry[0:11] = b'BOOT       '
        entry[11] = 0x10               # Attribute: Directory
        first_cluster_boot_dir = 3
        struct.pack_into('<H', entry, 26, first_cluster_boot_dir)
        f.write(entry)

        # Write BOOT directory cluster
        boot_dir_offset = data_start * sector_size + (first_cluster_boot_dir - 2) * sectors_per_cluster * sector_size
        f.seek(boot_dir_offset)

        # Entry in BOOT dir: BOOTX64.EFI file
        entry = bytearray(32)
        entry[0:11] = b'BOOTX64 EFI'  # 8.3 format: BOOTX64.EFI
        entry[11] = 0x20               # Attribute: Archive (normal file)
        first_cluster_bootfile = 4
        struct.pack_into('<H', entry, 26, first_cluster_bootfile)
        struct.pack_into('<I', entry, 28, efi_size)  # File size
        f.write(entry)

        # Write the actual EFI file data
        file_offset = data_start * sector_size + (first_cluster_bootfile - 2) * sectors_per_cluster * sector_size
        f.seek(file_offset)
        f.write(efi_data)

        # Update FAT to mark clusters as used
        f.seek(fat1_start * sector_size)
        fat = bytearray(f.read(sectors_per_fat * sector_size))

        # FAT12 uses 12 bits per entry (1.5 bytes)
        def set_fat_entry(fat_data, cluster, value):
            offset = cluster + (cluster // 2)  # cluster * 1.5
            if cluster % 2 == 0:
                # Even cluster: lower 12 bits
                fat_data[offset] = value & 0xFF
                fat_data[offset + 1] = (fat_data[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)
            else:
                # Odd cluster: upper 12 bits
                fat_data[offset] = (fat_data[offset] & 0x0F) | ((value << 4) & 0xF0)
                fat_data[offset + 1] = (value >> 4) & 0xFF

        # Mark EFI dir (cluster 2)
        set_fat_entry(fat, 2, 0xFFF)  # End of chain

        # Mark BOOT dir (cluster 3)
        set_fat_entry(fat, 3, 0xFFF)  # End of chain

        # Mark BOOTX64.EFI clusters (starting at cluster 4)
        for i in range(efi_clusters_needed):
            cluster_num = first_cluster_bootfile + i
            if i == efi_clusters_needed - 1:
                set_fat_entry(fat, cluster_num, 0xFFF)  # Last cluster
            else:
                set_fat_entry(fat, cluster_num, cluster_num + 1)  # Point to next

        # Write updated FAT (both copies)
        f.seek(fat1_start * sector_size)
        f.write(fat)
        f.seek(fat2_start * sector_size)
        f.write(fat)

    print(f"✓ Added /EFI/BOOT/BOOTX64.EFI to disk image")
    print(f"✓ Disk image created successfully: {image_path}")
    return True

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: create_uefi_disk.py <output_image> <efi_file> [size_mb]")
        sys.exit(1)

    output_image = sys.argv[1]
    efi_file = sys.argv[2]
    size_mb = int(sys.argv[3]) if len(sys.argv) > 3 else 50

    success = create_fat12_boot_disk(output_image, efi_file, size_mb)
    sys.exit(0 if success else 1)
