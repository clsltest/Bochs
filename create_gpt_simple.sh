#!/bin/bash
set -e

echo "Creating GPT UEFI disk with mtools..."

# Create 100MB disk
dd if=/dev/zero of=uefi-gpt-disk.img bs=1M count=100 2>/dev/null

# Partition with gdisk
gdisk uefi-gpt-disk.img << EOF
o
y
n
1

+98M
ef00
w
y
EOF

# Extract partition
# Partition starts at sector 2048 (1MB)
PART_START=2048
PART_SIZE=$((98 * 1024 * 1024 / 512))  # 98MB in sectors

dd if=uefi-gpt-disk.img of=partition.tmp bs=512 skip=$PART_START count=$PART_SIZE 2>/dev/null

# Format as FAT32
mkfs.vfat -F 32 -n "EFISYSTEM" partition.tmp > /dev/null

# Use mtools to copy files (no mounting needed!)
echo "drive z: file=\"partition.tmp\" partition=1" > mtoolsrc
export MTOOLSRC=$PWD/mtoolsrc

# Create directory structure
mmd -i partition.tmp ::EFI
mmd -i partition.tmp ::EFI/BOOT

# Copy UEFI Shell
mcopy -i partition.tmp bochs/bios/shell.efi ::EFI/BOOT/BOOTX64.EFI

# List contents to verify
echo "Contents of EFI partition:"
mdir -i partition.tmp -/

# Write partition back
dd if=partition.tmp of=uefi-gpt-disk.img bs=512 seek=$PART_START conv=notrunc 2>/dev/null

# Cleanup
rm partition.tmp mtoolsrc

echo "✓ Created uefi-gpt-disk.img"
ls -lh uefi-gpt-disk.img

