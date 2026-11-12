#!/usr/bin/env python3
"""Flash an OTA image to SR08 rings over BLE from this computer (Dialog SUOTA protocol),
with the same settings as the Android SUOTA app (docs/OTA_Settings_for_apk.jpg).
Requires: pip install bleak

  suota_flash.py --scan                          list nearby rings, strongest signal first
  suota_flash.py output.bin                      flash the nearest ring
  suota_flash.py output.bin --name LibRing       only rings named *LibRing* (custom firmware)
  suota_flash.py output.bin --min-rssi -55       only rings close to the computer
  suota_flash.py output.bin --address AA:BB:...  one specific ring
  suota_flash.py output.bin --ring C7 --ring 72  the rings named LibRing_C7 and LibRing_72
  suota_flash.py output.bin --all                every matching ring, one after the other

Rings running the stock firmware advertise their product ID (manufacturer data containing
0001XXXX) and reject an image built for another ID, so those rings are skipped.

Each ring keeps its BLE address: the default address compiled in the firmware is replaced
in the image by the address the ring advertises (its factory address for a stock ring).
Except the addresses the firmware derives from the flash unique ID (48:23:35:XX:YY:ZZ):
the image keeps the default one, so that the firmware derives it again (and a fix of
that derivation applies). --set-address gives one ring a specific address instead.

--save writes the per-ring images (output_<ADDRESS>.bin, next to the input image)
instead of flashing, e.g. for the Android SUOTA app:
  suota_flash.py output.bin --save --all                        every ring in range
  suota_flash.py output.bin --save --set-address 02:05:5E:94:54:36   no scan needed

macOS: CoreBluetooth hides BLE addresses (each ring shows as a UUID, which --address
also accepts), so the ring's address can't be kept: the image keeps the default one and
the firmware derives an address from the flash unique ID (48:23:35:XX:YY:ZZ), as with a
plain output.bin in the Android app. A ring that had its factory address gets a new one
(and a new LibRing_XX name). --set-address still gives a ring a specific address.
The terminal app needs the Bluetooth permission (System Settings > Privacy & Security >
Bluetooth, asked at the first scan): Terminal or iTerm work, a terminal embedded in an
app without it (some IDEs) gets the script killed by macOS (abort, exit code 134).

If connections keep dropping, raising Linux's default BLE supervision timeout (420 ms)
may help (units of 10 ms), then restart Bluetooth:
  sudo sed -i 's/^#ConnectionSupervisionTimeout=$/ConnectionSupervisionTimeout=400/' /etc/bluetooth/main.conf
  sudo systemctl restart bluetooth
"""
import argparse
import asyncio
import os
import re
import struct
import sys
import time
import zlib

from bleak import BleakClient, BleakScanner

SUOTA_SERVICE        = "0000fef5-0000-1000-8000-00805f9b34fb"
MEM_DEV              = "8082caa8-41a6-4021-91c6-56f9b954cc34"
GPIO_MAP             = "724249f0-5ec3-4b5f-8804-42345af08651"
PATCH_LEN            = "9d84b9a3-000c-49d8-9183-855b673fda31"
PATCH_DATA           = "457871e8-d516-4ca1-9116-57d0b17b9cb2"
SERV_STATUS          = "5f78df94-798c-46f5-990a-b3eb6a065c88"

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


# Default BLE address compiled in the firmware (CFG_NVDS_TAG_BD_ADDRESS, little endian:
# 48:23:35:F4:00:10). The build has 2 copies of it, both from that define: patching them
# gives the same image as compiling with another address.
DEFAULT_BD_ADDR = bytes([0x10, 0x00, 0xF4, 0x35, 0x23, 0x48])
DEFAULT_BD_ADDR_COPIES = 2

# Addresses derived by the firmware from the flash unique ID: 48:23:35 + 3 bytes
# (user_bd_address_init() in src/user_proxr.c)
DERIVED_BD_ADDR_PREFIX = "48:23:35:"


def is_bd_address(address):
    """True for "AA:BB:CC:DD:EE:FF", False for the UUIDs macOS gives instead."""
    return re.fullmatch(r"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", address) is not None


def with_bd_address(image, address):
    """Image with the default BLE address replaced by address ("AA:BB:CC:DD:EE:FF")."""
    if not is_bd_address(address):
        raise ValueError(f"not a BLE address: {address}")
    new = bytes.fromhex(address.replace(":", ""))[::-1]
    code_size = struct.unpack_from("<I", image, 33)[0]
    code = image[64:64 + code_size]
    if code.count(DEFAULT_BD_ADDR) != DEFAULT_BD_ADDR_COPIES:
        raise ValueError(f"expected {DEFAULT_BD_ADDR_COPIES} copies of the default address "
                         f"in the image, found {code.count(DEFAULT_BD_ADDR)}")
    code = code.replace(DEFAULT_BD_ADDR, new)
    out = bytearray(image[:64] + code + image[64 + code_size:])
    struct.pack_into("<I", out, 8, zlib.crc32(code) & 0xFFFFFFFF)  # header CRC32
    xor = 0
    for b in out[:-1]:
        xor ^= b
    out[-1] = xor
    return bytes(out)


# Our firmware's manufacturer data (src/firmware_version.c): company ID 0xFFFF, commit
# hash (4 bytes, big-endian), flags (bit 0: uncommitted changes, bit 1: unknown)
VERSION_COMPANY_ID = 0xFFFF


def advertised_version(adv):
    """Commit our firmware was built from (e.g. '446f379', '446f379+' if modified),
    'unknown' if built without git, else None (stock firmware, older builds)."""
    data = adv.manufacturer_data.get(VERSION_COMPANY_ID)
    if data is None or len(data) != 5:
        return None
    if data[4] & 0x02:
        return "unknown"
    return data[:4].hex()[:7] + ("+" if data[4] & 0x01 else "")


def advertised_product_id(adv):
    """Product ID from stock firmware manufacturer data (e.g. ...6F 00010017 00), else None."""
    for company_id, data in adv.manufacturer_data.items():
        if company_id == VERSION_COMPANY_ID and len(data) == 5:
            continue  # our firmware version, not a product ID
        raw = struct.pack("<H", company_id) + data
        for tail in (raw[-4:], raw[-5:-1]):  # 0001XXXX, optionally followed by 1 byte
            if len(tail) == 4 and tail[:3] == b"\x00\x01\x00":
                return tail[3]
    return None


def looks_like_ring(dev, adv):
    return (SUOTA_SERVICE in adv.service_uuids
            or advertised_product_id(adv) is not None
            or (dev.name or adv.local_name) in ("LibRing", "TEST"))  # TEST: before the rename


def ring_name(name):
    """--ring value -> advertised name, upper case: 'c7' or 'LibRing_C7' -> 'LIBRING_C7'."""
    name = name.upper()
    return name if name.startswith("LIBRING_") else "LIBRING_" + name


async def scan(args):
    found = {}
    seen = asyncio.Event()
    wanted = {ring_name(r) for r in args.ring or ()}
    seen_names = set()

    def on_adv(dev, adv):
        found[dev.address] = (dev, adv)
        if args.address and dev.address.upper() == args.address.upper():
            seen.set()
        name = (adv.local_name or dev.name or "").upper()
        if name in wanted:
            seen_names.add(name)
            if seen_names == wanted:
                seen.set()

    async with BleakScanner(on_adv):
        if args.address or wanted:
            # stop as soon as the ring(s) are seen (+1 s for scan responses)
            try:
                await asyncio.wait_for(seen.wait(), args.scan_time)
                await asyncio.sleep(1)
            except asyncio.TimeoutError:
                pass
        else:
            await asyncio.sleep(args.scan_time)

    rings = []
    for dev, adv in found.values():
        name = adv.local_name or dev.name or ""
        if args.address and dev.address.upper() != args.address.upper():
            continue
        if args.name and args.name.lower() not in name.lower():
            continue
        if wanted and name.upper() not in wanted:
            continue
        if not args.address and not args.name and not looks_like_ring(dev, adv):
            continue
        if adv.rssi < args.min_rssi:
            continue
        pid = advertised_product_id(adv)
        if args.product_id is not None and pid != args.product_id:
            continue
        rings.append((adv.rssi, dev, name, pid, advertised_version(adv)))
    rings.sort(key=lambda r: r[0], reverse=True)
    return rings


def print_rings(rings):
    width = max(17, *(len(r[1].address) for r in rings))  # macOS: 36-character UUIDs
    print(f"{'address':{width}s}  {'rssi':>4s}  {'name':12s}  {'product ID':10s}  firmware")
    for rssi, dev, name, pid, version in rings:
        pid_str = f"0x{pid:02X}" if pid is not None else "-"
        fw = f"commit {version}" if version else ("stock" if pid is not None else "-")
        print(f"{dev.address:{width}s}  {rssi:4d}  {name:12s}  {pid_str:10s}  {fw}")


async def flash(dev, image, args, stock_firmware):
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

        # Our firmware: 240-byte writes and 480-byte blocks (2 writes per status
        # notification, its buffer takes up to 512 bytes): ~5x faster than the 20-byte
        # writes and 240-byte blocks of the Android app (45 KB: 8.7 s instead of 43 s).
        # The stock receiver is untested with them: 20/240 there. (Its errors on reads
        # of the optional SUOTA 1.3 characteristics prevent asking the ring.)
        chunk = args.chunk or (20 if stock_firmware else 240)
        block_size = args.block_size or (240 if stock_firmware else 480)
        # A write must fit in one ATT packet. On BlueZ, bleak reports the default MTU
        # (23) unless asked for the negotiated one (247 on our firmware). Not on stock
        # rings: they use 20-byte writes anyway, and asking breaks the connection
        # ("GATT Protocol Error: Unlikely Error", then disconnections).
        if chunk > 20:
            try:
                await client._backend._acquire_mtu()
            except Exception:
                pass
            chunk = min(chunk, client.mtu_size - 3)

        await client.write_gatt_char(MEM_DEV, struct.pack("<I", (MEM_SPI_FLASH << 24) | args.bank), response=True)
        await expect(ST_IMG_STARTED, "start")
        await client.write_gatt_char(GPIO_MAP, struct.pack("<I", GPIO_MAP_SR08), response=True)

        start = time.monotonic()
        patch_len = None
        for offset in range(0, len(image), block_size):
            block = image[offset:offset + block_size]
            if len(block) != patch_len:
                patch_len = len(block)
                await client.write_gatt_char(PATCH_LEN, struct.pack("<H", patch_len), response=True)
            for i in range(0, len(block), chunk):
                await client.write_gatt_char(PATCH_DATA, block[i:i + chunk], response=False)
            # the first block also erases the image area in flash, which takes longer
            await expect(ST_CMP_OK, f"block at 0x{offset:X}", timeout=30 if offset == 0 else 10)
            done = offset + len(block)
            print(f"\r  {done * 100 // len(image):3d}%  {done}/{len(image)} bytes", end="", flush=True)
        print(f"  ({time.monotonic() - start:.1f} s, {chunk}-byte writes, {block_size}-byte blocks)")

        await client.write_gatt_char(MEM_DEV, struct.pack("<I", CMD_IMG_END << 24), response=True)
        try:
            await expect(ST_CMP_OK, "image check")
        except SuotaError as e:
            # The stock receiver reports a CRC error although the image boots fine
            # (the Android app offers to skip it). Never ignore it on our firmware.
            if not stock_firmware:
                raise
            print(f"  {e}: ignored on stock firmware, like 'skip' in the SUOTA app")
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
    parser.add_argument("--ring", action="append", metavar="NAME",
                        help="ring named exactly NAME, e.g. LibRing_C7 or C7 (repeat for several; "
                             "flashes all of them)")
    parser.add_argument("--min-rssi", type=int, default=-100, help="ignore rings with a weaker signal (e.g. -55)")
    parser.add_argument("--product-id", type=lambda x: int(x, 16), help="only stock rings advertising this ID (hex)")
    parser.add_argument("--all", action="store_true", help="flash every matching ring instead of the nearest one")
    parser.add_argument("--force", action="store_true", help="flash even if the advertised product ID differs")
    parser.add_argument("--set-address", help="BLE address to give the ring (one ring only), "
                        "instead of keeping the address it advertises")
    parser.add_argument("--raw", action="store_true",
                        help="flash the image unchanged (e.g. a stock firmware: no BLE address to set)")
    parser.add_argument("--save", action="store_true",
                        help="write output_<ADDRESS>.bin per ring instead of flashing")
    parser.add_argument("--scan-time", type=float, default=5.0, help="scan duration in seconds (default: 5)")
    parser.add_argument("--retries", type=int, default=5,
                        help="attempts per ring on connection problems (default: 5)")
    parser.add_argument("--chunk", type=int,
                        help="bytes per BLE write (default: 240 on our firmware, 20 on stock rings)")
    parser.add_argument("--block-size", type=int,
                        help="SUOTA block size, at most 512 (default: 480 on our firmware, 240 on stock rings)")
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

    def save(address):
        path = os.path.join(os.path.dirname(args.image), f"output_{address.replace(':', '')}.bin")
        with open(path, "wb") as f:
            f.write(with_bd_address(image, address))
        print(f"  saved {path} (BLE address {address})")

    if args.save and args.set_address:
        save(args.set_address.upper())  # no ring needed
        return

    if args.address:
        print(f"Looking for {args.address} (up to {args.scan_time:g} s)...")
    elif args.ring:
        names = sorted({ring_name(r) for r in args.ring})
        print(f"Looking for {', '.join('LibRing_' + n[8:] for n in names)} (up to {args.scan_time:g} s)...")
    else:
        print(f"Scanning for {args.scan_time:g} s...")
    rings = await scan(args)
    missing = sorted({ring_name(r) for r in args.ring or ()} - {r[2].upper() for r in rings})
    if missing:
        print(f"Not found: {', '.join('LibRing_' + n[8:] for n in missing)}")
    if not rings:
        sys.exit("No matching ring found (is it advertising, i.e. not connected to another app?)")
    print_rings(rings)
    if args.scan:
        return

    targets = rings if args.all or args.ring else rings[:1]
    if args.set_address and len(targets) > 1:
        sys.exit("--set-address needs a single ring (don't use it with --all)")
    failed = []
    for n, (rssi, dev, name, pid, _) in enumerate(targets, 1):
        print(f"\n[{n}/{len(targets)}] {dev.address} {name} (rssi {rssi})")
        if pid is not None and pid != image_pid and not args.force:
            print(f"  skipped: ring product ID 0x{pid:02X}, image built for 0x{image_pid:02X}"
                  f" (rebuild with: make PRODUCT_ID={pid:02X})")
            failed.append(dev.address)
            continue
        # Keep the ring's BLE address (or set the requested one), unless the firmware
        # derives it: then keep the default one in the image. Same when the address is
        # unknown (macOS): the firmware then derives one.
        address = (args.set_address or dev.address).upper()
        hidden_address = not args.set_address and not is_bd_address(address)
        if hidden_address and args.save and not args.raw:
            print("  skipped: --save needs the ring's BLE address, hidden on macOS"
                  " (give one with --set-address)")
            failed.append(dev.address)
            continue
        if not args.set_address and (hidden_address or address.startswith(DERIVED_BD_ADDR_PREFIX)):
            address = DEFAULT_BD_ADDR[::-1].hex(":").upper()
        try:
            ring_image = image if args.raw else with_bd_address(image, address)
        except ValueError as e:
            print(f"  skipped: {e}")
            failed.append(dev.address)
            continue
        if args.save:
            save(address)
            continue
        if args.raw:
            print("  image flashed unchanged (--raw)")
        elif hidden_address:
            print("  BLE address after flashing: derived by the firmware (48:23:35:XX:YY:ZZ);"
                  " macOS hides the current one, so it can't be kept")
        elif address.startswith(DERIVED_BD_ADDR_PREFIX):
            print("  BLE address after flashing: derived by the firmware (48:23:35:XX:YY:ZZ)")
        else:
            print(f"  BLE address after flashing: {address}")
        if n > 1:
            # BlueZ forgets the devices seen in the first scan after a while
            dev = await BleakScanner.find_device_by_address(dev.address, timeout=10) or dev
        # Connections often drop (see the supervision timeout above): retry, the
        # transfer restarts from the beginning.
        for attempt in range(1, args.retries + 1):
            try:
                await flash(dev, ring_image, args, stock_firmware=pid is not None)
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
                    # BlueZ forgets a device after failed connections: scan for it again
                    dev = await BleakScanner.find_device_by_address(dev.address, timeout=10) or dev
    default_address = DEFAULT_BD_ADDR[::-1].hex(":").upper()
    if len(targets) > 1 and any(dev.address.upper() == default_address for _, dev, *_ in targets):
        print(f"\nRings sharing the default address {default_address} (old firmware) are "
              "flashed one per run: run again until none is left")
    if failed:
        sys.exit(f"\n{len(failed)} ring(s) not flashed: {', '.join(failed)}")


if __name__ == "__main__":
    asyncio.run(main())
