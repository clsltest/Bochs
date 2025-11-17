#!/bin/bash
# Try to see what fw_cfg files QEMU exposes

echo "Testing QEMU with fw_cfg debug..."

# Run QEMU briefly with monitor to check fw_cfg
timeout 5 qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file=bochs/bios/OVMF_CODE.fd \
  -drive file=uefi-gpt-disk.img,format=raw \
  -m 256 \
  -nographic \
  -monitor stdio <<EOF 2>&1 | tee qemu-fwcfg-check.log
info qtree
quit
EOF

echo ""
echo "Checking for fw_cfg references in QEMU source/docs..."

