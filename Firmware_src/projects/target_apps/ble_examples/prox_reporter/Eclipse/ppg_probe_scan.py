#!/usr/bin/env python3
"""Read the PPG probe results of rings running the ppg branch firmware.

The firmware probes the PPG sensor 7 s after boot and puts the results in its scan
response (see ppg_probe() in src/ppg/hx3605.h). They show after the first
connection + disconnection following the probe: --refresh does it.
Requires: pip install bleak
"""
import argparse
import asyncio
import struct

from bleak import BleakClient, BleakScanner

PROBE_LEN = 29
FLAGS = ["read before wake failed", "wake write failed", "read after wake failed",
         "register dump failed", "sleep write failed"]


def decode(raw):
    if raw[0] != 0x50:
        return ["  probe not run yet (or skipped): use --refresh, at least 7 s after boot"]
    devices = [a for a in range(0x08, 0x78) if raw[1 + (a - 8) // 8] >> ((a - 8) % 8) & 1]
    flags = [f for i, f in enumerate(FLAGS) if raw[18] >> i & 1]
    return [
        f"  I2C devices    : {' '.join(f'0x{a:02X}' for a in devices) or 'none'}",
        f"  chip ID        : 0x{raw[16]:02X} before wake, 0x{raw[17]:02X} after wake"
        "  (stock firmware expects 0x27, datasheet 0x25)",
        f"  regs 0x01-0x08 : {raw[19:27].hex(' ').upper()}",
        f"  errors         : {', '.join(flags) or 'none'}",
        f"  accel WHO_AM_I : 0x{raw[27]:02X} (0x11 expected), I2C target after: 0x{raw[28]:02X}",
    ]


async def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--address", help="only this ring")
    parser.add_argument("--refresh", action="store_true",
                        help="connect and disconnect first, so that the ring restarts "
                             "advertising with the latest results")
    parser.add_argument("--scan-time", type=float, default=10.0)
    args = parser.parse_args()

    if args.refresh:
        if not args.address:
            parser.error("--refresh needs --address")
        dev = await BleakScanner.find_device_by_address(args.address, timeout=15)
        if dev:
            async with BleakClient(dev, timeout=20):
                pass
            await asyncio.sleep(2)

    found = {}

    def on_adv(dev, adv):
        name = adv.local_name or dev.name or ""
        if not name.startswith("LibRing"):
            return
        if args.address and dev.address.upper() != args.address.upper():
            return
        for cid, data in adv.manufacturer_data.items():
            raw = struct.pack("<H", cid) + data
            if len(raw) == PROBE_LEN:
                found[dev.address] = (name, adv.rssi, raw)

    async with BleakScanner(on_adv):
        await asyncio.sleep(args.scan_time)

    if not found:
        print("No ring with the ppg probe firmware found")
    for address, (name, rssi, raw) in sorted(found.items()):
        print(f"{address}  {name}  rssi {rssi}")
        print("\n".join(decode(raw)))
        print(f"  raw            : {raw.hex()}")


asyncio.run(main())
