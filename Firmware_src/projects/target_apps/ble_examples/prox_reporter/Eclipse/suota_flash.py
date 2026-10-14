#!/usr/bin/env python3
"""Flash an OTA image to SR08 rings over BLE from this computer (Dialog SUOTA protocol),
with the same settings as the Android SUOTA app (docs/OTA_Settings_for_apk.jpg).
Requires: pip install bleak

  suota_flash.py --scan                          list nearby rings, strongest signal first
  suota_flash.py output.bin                      flash the nearest ring
  suota_flash.py output.bin --name TEST          only rings named *TEST* (custom firmware)
  suota_flash.py output.bin --min-rssi -55       only rings close to the computer
  suota_flash.py output.bin --address AA:BB:...  one specific ring
  suota_flash.py output.bin --all                every matching ring, one after the other

Rings running the stock firmware advertise their product ID (manufacturer data ending
with 0001XXXX) and reject an image built for another ID, so those rings are skipped.
"""
import argparse
import asyncio
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

SUOTA_SERVICE        = "0000fef5-0000-1000-8000-00805f9b34fb"
MEM_DEV              = "8082caa8-41a6-4021-91c6-56f9b954cc34"
GPIO_MAP             = "724249f0-5ec3-4b5f-8804-42345af08651"
PATCH_LEN            = "9d84b9a3-000c-49d8-9183-855b673fda31"
PATCH_DATA           = "457871e8-d516-4ca1-9116-57d0b17b9cb2"
SERV_STATUS          = "5f78df94-798c-46f5-990a-b3eb6a065c88"
PATCH_DATA_CHAR_SIZE = "42c3dfdd-77be-4d9c-8454-8f875267fb3b"  # SUOTA >= 1.3 only

MEM_SPI_FLASH = 0x13
CMD_IMG_END   = 0xFE
CMD_REBOOT    = 0xFD
# SPI flash pins, one nibble pair (port, pin) each: MISO P0_5, MOSI P0_6, CS P0_3, SCK P0_0
GPIO_MAP_SR08 = 0x05060300

ST_SRV_STARTED, ST_CMP_OK, ST_IMG_STARTED = 0x01, 0x02, 0x10
STATUS = {
    0x01: "service started", 0x02: "OK", 0x03: "service exit", 0x04: "CRC error",
    0x05: "patch length error", 0x06: "flash write error", 0x07: "internal memory error",
    0x08: "invalid memory type", 0x09: "application error", 0x10: "image started",
    0x11: "invalid image bank", 0x12: "invalid image header (wrong product ID?)",
    0x13: "invalid image size", 0x14: "invalid product header", 0x15: "same image",
    0x16: "flash read error",
}


class SuotaError(RuntimeError):
    """The ring refused the image: retrying won't help."""


def advertised_product_id(adv):
    """Product ID from manufacturer data ending with 0001XXXX (stock firmware), else None."""
    for company_id, data in adv.manufacturer_data.items():
        raw = struct.pack("<H", company_id) + data
        if len(raw) >= 4 and raw[-4:-2] == b"\x00\x01":
            return struct.unpack(">H", raw[-2:])[0]
    return None


def looks_like_ring(dev, adv):
    return (SUOTA_SERVICE in adv.service_uuids
            or advertised_product_id(adv) is not None
            or (dev.name or adv.local_name) == "TEST")


async def scan(args):
    found = await BleakScanner.discover(timeout=args.scan_time, return_adv=True)
    rings = []
    for dev, adv in found.values():
        name = adv.local_name or dev.name or ""
        if args.address and dev.address.upper() != args.address.upper():
            continue
        if args.name and args.name.lower() not in name.lower():
            continue
        if not args.address and not args.name and not looks_like_ring(dev, adv):
            continue
        if adv.rssi < args.min_rssi:
            continue
        pid = advertised_product_id(adv)
        if args.product_id is not None and pid != args.product_id:
            continue
        rings.append((adv.rssi, dev, name, pid))
    rings.sort(key=lambda r: r[0], reverse=True)
    return rings


def print_rings(rings):
    print(f"{'address':17s}  {'rssi':>4s}  {'name':12s}  product ID")
    for rssi, dev, name, pid in rings:
        pid_str = f"0x{pid:02X}" if pid is not None else "- (custom fw)"
        print(f"{dev.address:17s}  {rssi:4d}  {name:12s}  {pid_str}")


async def flash(dev, image, args):
    status = asyncio.Queue()
    connected_at = time.monotonic()

    async def expect(wanted, step, timeout=10):
        while True:
            try:
                st = await asyncio.wait_for(status.get(), timeout)
            except asyncio.TimeoutError:
                raise ConnectionError(f"{step}: no answer from the ring") from None
            if st is None:
                raise ConnectionError(f"{step}: ring disconnected "
                                      f"{time.monotonic() - connected_at:.1f} s after connecting")
            if st != ST_SRV_STARTED:
                break
        if st != wanted:
            raise SuotaError(f"{step}: {STATUS.get(st, f'status 0x{st:02X}')}")

    async with BleakClient(dev, timeout=20,
                           disconnected_callback=lambda _: status.put_nowait(None)) as client:
        connected_at = time.monotonic()
        await client.start_notify(SERV_STATUS, lambda _, data: status.put_nowait(data[0]))

        # Data chunk size: as large as the ring and the connection allow
        chunk = 20
        try:
            if args.chunk:
                raise ValueError
            chunk = struct.unpack("<H", await client.read_gatt_char(PATCH_DATA_CHAR_SIZE))[0]
            await client._backend._acquire_mtu()
            chunk = min(chunk, client.mtu_size - 3)
        except Exception:
            chunk = 20
        chunk = args.chunk or max(20, min(chunk, args.block_size))

        await client.write_gatt_char(MEM_DEV, struct.pack("<I", (MEM_SPI_FLASH << 24) | args.bank), response=True)
        await expect(ST_IMG_STARTED, "start")
        await client.write_gatt_char(GPIO_MAP, struct.pack("<I", GPIO_MAP_SR08), response=True)

        start = time.monotonic()
        patch_len = None
        for offset in range(0, len(image), args.block_size):
            block = image[offset:offset + args.block_size]
            if len(block) != patch_len:
                patch_len = len(block)
                await client.write_gatt_char(PATCH_LEN, struct.pack("<H", patch_len), response=True)
            for i in range(0, len(block), chunk):
                await client.write_gatt_char(PATCH_DATA, block[i:i + chunk], response=False)
            # the first block also erases the image area in flash, which takes longer
            await expect(ST_CMP_OK, f"block at 0x{offset:X}", timeout=30 if offset == 0 else 10)
            done = offset + len(block)
            print(f"\r  {done * 100 // len(image):3d}%  {done}/{len(image)} bytes", end="", flush=True)
        print(f"  ({time.monotonic() - start:.1f} s, {chunk}-byte chunks)")

        await client.write_gatt_char(MEM_DEV, struct.pack("<I", CMD_IMG_END << 24), response=True)
        await expect(ST_CMP_OK, "image check")
        try:
            # The ring disconnects to reboot, which may interrupt this write
            await client.write_gatt_char(MEM_DEV, struct.pack("<I", CMD_REBOOT << 24), response=True)
        except Exception:
            pass


async def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", nargs="?", help="OTA image (output.bin)")
    parser.add_argument("--scan", action="store_true", help="only list the matching rings")
    parser.add_argument("--name", help="advertised name contains this (case-insensitive)")
    parser.add_argument("--address", help="BLE address of one ring")
    parser.add_argument("--min-rssi", type=int, default=-100, help="ignore rings with a weaker signal (e.g. -55)")
    parser.add_argument("--product-id", type=lambda x: int(x, 16), help="only stock rings advertising this ID (hex)")
    parser.add_argument("--all", action="store_true", help="flash every matching ring instead of the nearest one")
    parser.add_argument("--force", action="store_true", help="flash even if the advertised product ID differs")
    parser.add_argument("--scan-time", type=float, default=5.0, help="scan duration in seconds (default: 5)")
    parser.add_argument("--retries", type=int, default=3,
                        help="attempts per ring on connection problems (default: 3)")
    parser.add_argument("--chunk", type=int, help="bytes per BLE write (default: largest possible)")
    parser.add_argument("--block-size", type=int, default=240, help="SUOTA block size (default: 240)")
    parser.add_argument("--bank", type=int, choices=(0, 1, 2), default=0, help="image bank, 0 = oldest (default)")
    args = parser.parse_args()
    if not args.scan and not args.image:
        parser.error("give an image to flash, or --scan")

    image = None
    if args.image:
        image = open(args.image, "rb").read()
        if image[:2] != b"\x70\x51":
            sys.exit(f"{args.image}: not an OTA image (run make_bin_ota.py)")
        image_pid = struct.unpack_from("<H", image, 39)[0]

    print(f"Scanning for {args.scan_time:g} s...")
    rings = await scan(args)
    if not rings:
        sys.exit("No matching ring found (is it advertising, i.e. not connected to another app?)")
    print_rings(rings)
    if args.scan:
        return

    targets = rings if args.all else rings[:1]
    failed = []
    for n, (rssi, dev, name, pid) in enumerate(targets, 1):
        print(f"\n[{n}/{len(targets)}] {dev.address} {name} (rssi {rssi})")
        if pid is not None and pid != image_pid and not args.force:
            print(f"  skipped: ring product ID 0x{pid:02X}, image built for 0x{image_pid:02X}"
                  f" (rebuild with: make PRODUCT_ID={pid:02X})")
            failed.append(dev.address)
            continue
        # The first connection to a ring often drops during GATT service discovery
        # (BlueZ caches the services once it succeeds), so retry connection problems.
        for attempt in range(1, args.retries + 1):
            try:
                await flash(dev, image, args)
                print("  done, the ring is rebooting")
                break
            except SuotaError as e:
                print(f"\n  FAILED: {e}")
                failed.append(dev.address)
                break
            except Exception as e:
                print(f"\n  connection problem ({attempt}/{args.retries}): {e or type(e).__name__}")
                if attempt == args.retries:
                    failed.append(dev.address)
                else:
                    await asyncio.sleep(2)
    if failed:
        sys.exit(f"\n{len(failed)} ring(s) not flashed: {', '.join(failed)}")


if __name__ == "__main__":
    asyncio.run(main())
