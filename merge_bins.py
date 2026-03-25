#!/usr/bin/env python3
"""
Merge ESP32-S3 bootloader, partitions, and firmware into a single binary
for flashing via ESP Web Flasher (https://espressif.github.io/esptool-js/).

Usage:
    python merge_bins.py          # Merged binary (bootloader + partitions + firmware)
    python merge_bins.py --ota    # OTA-only binary (firmware only)
"""

import argparse
import os
import sys

BUILD_DIR = '.pio/build/esp32s3-waveshare-4_3'
BOOTLOADER = os.path.join(BUILD_DIR, 'bootloader.bin')
PARTITIONS = os.path.join(BUILD_DIR, 'partitions.bin')
FIRMWARE = os.path.join(BUILD_DIR, 'firmware.bin')

OUTPUT_DIR = 'firmware'
OUTPUT_MERGED = os.path.join(OUTPUT_DIR, 'BambuHelper-WebFlasher.bin')
OUTPUT_OTA = os.path.join(OUTPUT_DIR, 'BambuHelper-OTA.bin')

# ESP32-S3 standard flash offsets
BOOTLOADER_OFFSET = 0x0
PARTITIONS_OFFSET = 0x8000
FIRMWARE_OFFSET = 0x10000


def create_ota_binary():
    """Copy firmware.bin as OTA update file."""
    if not os.path.exists(FIRMWARE):
        print(f"Error: {FIRMWARE} not found. Run 'pio run' first.")
        return False

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    with open(FIRMWARE, 'rb') as src, open(OUTPUT_OTA, 'wb') as dst:
        dst.write(src.read())

    size = os.path.getsize(OUTPUT_OTA)
    print(f"OTA binary created: {OUTPUT_OTA} ({size / 1024:.1f} KB)")
    return True


def merge_binaries():
    """Merge bootloader + partitions + firmware into a single flashable binary."""
    for path in [BOOTLOADER, PARTITIONS, FIRMWARE]:
        if not os.path.exists(path):
            print(f"Error: {path} not found. Run 'pio run' first.")
            return False

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    with open(OUTPUT_MERGED, 'wb') as out:
        # Bootloader @ 0x0
        with open(BOOTLOADER, 'rb') as f:
            bl = f.read()
            out.write(bl)
            print(f"  Bootloader: {len(bl)} bytes @ 0x{BOOTLOADER_OFFSET:X}")

        # Pad to 0x8000
        out.write(b'\xFF' * (PARTITIONS_OFFSET - len(bl)))

        # Partitions @ 0x8000
        with open(PARTITIONS, 'rb') as f:
            pt = f.read()
            out.write(pt)
            print(f"  Partitions: {len(pt)} bytes @ 0x{PARTITIONS_OFFSET:X}")

        # Pad to 0x10000
        out.write(b'\xFF' * (FIRMWARE_OFFSET - PARTITIONS_OFFSET - len(pt)))

        # Firmware @ 0x10000
        with open(FIRMWARE, 'rb') as f:
            fw = f.read()
            out.write(fw)
            print(f"  Firmware:   {len(fw)} bytes @ 0x{FIRMWARE_OFFSET:X}")

    total = os.path.getsize(OUTPUT_MERGED)
    print(f"\nMerged binary created: {OUTPUT_MERGED} ({total / 1024:.1f} KB)")
    print(f"\nFlash via ESP Web Flasher:")
    print(f"  1. Open https://espressif.github.io/esptool-js/")
    print(f"  2. Connect ESP32-S3 via USB")
    print(f"  3. Click 'Connect', select your device")
    print(f"  4. Set flash address: 0x0")
    print(f"  5. Select file: {OUTPUT_MERGED}")
    print(f"  6. Click 'Program'")
    return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Create flashable BambuHelper firmware')
    parser.add_argument('--ota', action='store_true', help='Create OTA-only binary')
    args = parser.parse_args()

    success = create_ota_binary() if args.ota else merge_binaries()
    sys.exit(0 if success else 1)
