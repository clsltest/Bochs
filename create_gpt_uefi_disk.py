#!/usr/bin/env python3
"""
Create a proper GPT disk with EFI System Partition and UEFI Shell
"""

import struct
import uuid
import os
import sys
import subprocess
from datetime import datetime

def create_gpt_disk(image_path, efi_file, size_mb=100):
    """Create a GPT disk with ESP containing UEFI boot file"""

    sector_size = 512
    total_sectors = (size_mb * 1024 * 1024) // sector_size

    # GPT header is at LBA 1, partition entries start at LBA 2
    # We'll create one partition: EFI System Partition

    # Partition starts at 2048 sectors (1MB aligned)
    partition_start_lba = 2048
    partition_end_lba = total_sectors - 34  # Leave 33 sectors for backup GPT
    partition_sectors = partition_end_lba - partition_start_lba + 1

    print(f"Creating {size_mb}MB GPT disk...")
    print(f"  Total sectors: {total_sectors}")
    print(f"  Partition LBA: {partition_start_lba} - {partition_end_lba}")
    print(f"  Partition size: {partition_sectors * sector_size // (1024*1024)}MB")

    # Create empty disk
    with open(image_path, 'wb') as f:
        f.write(b'\x00' * (total_sectors * sector_size))

    # Write protective MBR (LBA 0)
    with open(image_path, 'r+b') as f:
        # MBR boot signature
        f.seek(510)
        f.write(b'\x55\xAA')

        # Single partition entry covering whole disk (type 0xEE = GPT protective)
        f.seek(446)
        # Bootable flag (0x00), CHS start (ignored), type (0xEE)
        f.write(bytes([0x00, 0x00, 0x02, 0x00, 0xEE]))
        # CHS end (ignored), LBA start, LBA count
        f.write(bytes([0xFF, 0xFF, 0xFF]))
        f.write(struct.pack('<I', 1))  # Start at LBA 1
        f.write(struct.pack('<I', min(total_sectors - 1, 0xFFFFFFFF)))

    # Write GPT header (LBA 1)
    disk_guid = uuid.uuid4()
    part_guid = uuid.uuid4()

    with open(image_path, 'r+b') as f:
        f.seek(sector_size)  # LBA 1

        # GPT header
        header = bytearray(92)
        header[0:8] = b'EFI PART'  # Signature
        struct.pack_into('<I', header, 8, 0x00010000)  # Revision
        struct.pack_into('<I', header, 12, 92)  # Header size
        struct.pack_into('<I', header, 16, 0)  # CRC32 (calculated later)
        struct.pack_into('<I', header, 20, 0)  # Reserved
        struct.pack_into('<Q', header, 24, 1)  # Current LBA
        struct.pack_into('<Q', header, 32, total_sectors - 1)  # Backup LBA
        struct.pack_into('<Q', header, 40, 34)  # First usable LBA
        struct.pack_into('<Q', header, 48, total_sectors - 34)  # Last usable LBA
        header[56:72] = disk_guid.bytes  # Disk GUID
        struct.pack_into('<Q', header, 72, 2)  # Partition entry LBA
        struct.pack_into('<I', header, 80, 128)  # Number of partition entries
        struct.pack_into('<I', header, 84, 128)  # Size of partition entry
        struct.pack_into('<I', header, 88, 0)  # Partition entry array CRC32

        # Calculate CRC32 for header (skip CRC field itself)
        import zlib
        header_for_crc = bytes(header[:16]) + b'\x00\x00\x00\x00' + bytes(header[20:])
        crc = zlib.crc32(header_for_crc) & 0xFFFFFFFF
        struct.pack_into('<I', header, 16, crc)

        f.write(header)

    # Write partition entry (LBA 2)
    # EFI System Partition GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
    esp_type_guid = uuid.UUID('C12A7328-F81F-11D2-BA4B-00A0C93EC93B')

    with open(image_path, 'r+b') as f:
        f.seek(sector_size * 2)  # LBA 2

        entry = bytearray(128)
        entry[0:16] = esp_type_guid.bytes_le  # Partition type GUID
        entry[16:32] = part_guid.bytes  # Unique partition GUID
        struct.pack_into('<Q', entry, 32, partition_start_lba)  # First LBA
        struct.pack_into('<Q', entry, 40, partition_end_lba)  # Last LBA
        struct.pack_into('<Q', entry, 48, 0x0000000000000000)  # Attributes
        # Partition name: "EFI System" in UTF-16LE
        name = "EFI System".encode('utf-16le')
        entry[56:56+len(name)] = name

        f.write(entry)

    # Now format the partition as FAT32 using command-line tools
    # Extract partition to temporary file
    partition_offset = partition_start_lba * sector_size
    partition_size = partition_sectors * sector_size

    print(f"Creating FAT32 filesystem in partition...")

    # Use dd to extract partition, format it, write it back
    subprocess.run(['dd', 'if=' + image_path, 'of=partition.tmp',
                   f'bs={sector_size}', f'skip={partition_start_lba}',
                   f'count={partition_sectors}'], check=True,
                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Format as FAT32
    subprocess.run(['mkfs.vfat', '-F', '32', '-n', 'EFISYSTEM', 'partition.tmp'],
                  check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Mount, copy files, unmount (need sudo)
    os.makedirs('/tmp/gpt_mount', exist_ok=True)
    try:
        subprocess.run(['mount', '-o', 'loop', 'partition.tmp', '/tmp/gpt_mount'],
                      check=True)

        # Create EFI directory structure
        os.makedirs('/tmp/gpt_mount/EFI/BOOT', exist_ok=True)

        # Copy UEFI Shell
        subprocess.run(['cp', efi_file, '/tmp/gpt_mount/EFI/BOOT/BOOTX64.EFI'],
                      check=True)

        print("✓ Installed BOOTX64.EFI to EFI/BOOT/")

        # Unmount
        subprocess.run(['sync'], check=True)
        subprocess.run(['umount', '/tmp/gpt_mount'], check=True)
    finally:
        try:
            os.rmdir('/tmp/gpt_mount')
        except:
            pass

    # Write partition back to disk image
    subprocess.run(['dd', 'if=partition.tmp', 'of=' + image_path,
                   f'bs={sector_size}', f'seek={partition_start_lba}',
                   f'conv=notrunc'], check=True,
                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    os.unlink('partition.tmp')

    print(f"✓ GPT UEFI disk created: {image_path}")
    print(f"  Disk GUID: {disk_guid}")
    print(f"  Partition GUID: {part_guid}")

    return True

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: create_gpt_uefi_disk.py <output_image> <efi_file> [size_mb]")
        sys.exit(1)

    output_image = sys.argv[1]
    efi_file = sys.argv[2]
    size_mb = int(sys.argv[3]) if len(sys.argv) > 3 else 100

    create_gpt_disk(output_image, efi_file, size_mb)
