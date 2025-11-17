#!/bin/bash
# Automated ACPI verification script for Alpine Linux in Bochs

set -e

echo "=== ACPI Verification Test ==="
echo

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    pkill -9 bochs || true
    pkill -9 Xvfb || true
    rm -f /tmp/bochs-serial-in /tmp/bochs-serial-out
}

trap cleanup EXIT

# Ensure Xvfb is running
echo "Starting Xvfb..."
pkill -9 Xvfb || true
Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &
sleep 2
export DISPLAY=:99

# Create named pipes for serial communication
echo "Creating serial communication pipes..."
rm -f /tmp/bochs-serial-in /tmp/bochs-serial-out
mkfifo /tmp/bochs-serial-in
mkfifo /tmp/bochs-serial-out

# Start Bochs with modified serial config
echo "Starting Bochs with Alpine Linux..."
cat > /tmp/test-acpi-verify.bochsrc << 'EOF'
# Alpine Linux ACPI Verification Test
memory: guest=512, host=512
romimage: file=bochs/bios/OVMF_CODE_SERIAL.fd
vgaromimage: file=bochs/bios/VGABIOS-lgpl-latest-cirrus.bin
cpu: count=1, ips=50000000, reset_on_triple_fault=1, model=corei7_sandy_bridge_2600k
pci: enabled=1, chipset=i440fx
display_library: x
vga: extension=cirrus
log: bochs-verify.log
debugger_log: -
debug: action=ignore
info: action=report
error: action=report
panic: action=fatal
com1: enabled=1, mode=file, dev=/tmp/alpine-verify-serial.log
ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14
ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15
ata0-master: type=cdrom, path=alpine-virt.iso, status=inserted
boot: cdrom
mouse: enabled=0
keyboard: type=mf, serial_delay=250
EOF

# Start Bochs in background and capture serial output
rm -f /tmp/alpine-verify-serial.log
touch /tmp/alpine-verify-serial.log

./bochs -f /tmp/test-acpi-verify.bochsrc -q &
BOCHS_PID=$!

echo "Bochs PID: $BOCHS_PID"
echo "Waiting for Alpine to boot (60 seconds)..."

# Monitor serial output
tail -f /tmp/alpine-verify-serial.log &
TAIL_PID=$!

# Wait for boot to complete
sleep 60

# Kill tail
kill $TAIL_PID 2>/dev/null || true

echo
echo "=== Checking Bochs log for ACPI table-loader ==="
grep -i "table-loader" bochs-verify.log || echo "No table-loader messages found"

echo
echo "=== Checking OVMF serial output ==="
if grep -q "InstallAcpiTables: Not Found" /tmp/alpine-verify-serial.log; then
    echo "❌ ERROR: OVMF failed to find ACPI tables"
else
    echo "✓ OVMF did not report 'Not Found' error"
fi

if grep -q "installing ACPI tables" /tmp/alpine-verify-serial.log; then
    echo "✓ OVMF reported installing ACPI tables"
else
    echo "⚠ OVMF did not explicitly report installing ACPI tables"
fi

echo
echo "=== Alpine Boot Status ==="
if grep -q "Welcome to Alpine" /tmp/alpine-verify-serial.log; then
    echo "✓ Alpine Linux booted successfully"
else
    echo "❌ Alpine Linux did not boot to login prompt"
fi

# Kill Bochs
echo
echo "Stopping Bochs..."
kill $BOCHS_PID 2>/dev/null || true
sleep 2
pkill -9 bochs || true

echo
echo "=== Full serial output ==="
cat /tmp/alpine-verify-serial.log

cleanup
echo
echo "=== Test Complete ==="
