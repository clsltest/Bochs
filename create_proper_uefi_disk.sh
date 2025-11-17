#!/bin/bash
set -e

echo "Creating proper GPT UEFI disk image..."

# Create 100MB disk image
dd if=/dev/zero of=uefi-gpt-disk.img bs=1M count=100

# Create GPT partition table with EFI System Partition
parted -s uefi-gpt-disk.img mklabel gpt
parted -s uefi-gpt-disk.img mkpart primary fat32 1MiB 99MiB
parted -s uefi-gpt-disk.img set 1 esp on

# Show partition table
parted -s uefi-gpt-disk.img print

# Setup loop device to format the partition
LOOP_DEV=$(losetup -f)
echo "Using loop device: $LOOP_DEV"
losetup -P $LOOP_DEV uefi-gpt-disk.img

# Give it a moment for partition to appear
sleep 2

# Format as FAT32
if [ -e "${LOOP_DEV}p1" ]; then
    PART_DEV="${LOOP_DEV}p1"
else
    # Try without 'p'
    PART_DEV="${LOOP_DEV}1"
fi

echo "Formatting $PART_DEV as FAT32..."
mkfs.vfat -F 32 -n "EFI_SYSTEM" $PART_DEV

# Mount and install UEFI Shell
mkdir -p /tmp/efi_mount
mount $PART_DEV /tmp/efi_mount

# Create EFI directory structure
mkdir -p /tmp/efi_mount/EFI/BOOT

# Copy UEFI Shell as bootloader
cp bochs/bios/shell.efi /tmp/efi_mount/EFI/BOOT/BOOTX64.EFI

# Verify
echo "Contents of EFI partition:"
find /tmp/efi_mount -type f

# Unmount and cleanup
sync
umount /tmp/efi_mount
losetup -d $LOOP_DEV
rmdir /tmp/efi_mount

echo "✓ UEFI GPT disk created successfully: uefi-gpt-disk.img"
ls -lh uefi-gpt-disk.img

