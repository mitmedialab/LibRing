#!/usr/bin/env python3
"""Stream the raw PPG (heart rate sensor) data of a ring running the ppg branch.

  ppg_stream.py ADDRESS [--time 20] [--write REG=VAL ...] [--read REG,N ...] [--csv out.csv]

The ring runs the stock heart-rate setup (25 Hz) while subscribed. --write writes
registers after the start, --read reads registers (before and after streaming).
Packet format: ppg_read() in src/ppg/hx3605.h.
Requires: pip install bleak
"""
import argparse
import asyncio
import cmath
import csv
import math
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

PPG_CHAR = "e7da0718-b8b6-44b6-912c-e0bfb05d562d"
FS = 25.0  # Hz (ppg_start())


def u24(b, i):
    return b[i] | b[i + 1] << 8 | b[i + 2] << 16


def fifo_sample(b, i):
    """(value, phase flag) as hx3605_read_fifo_data(): 21-bit signed, flag in bits 21-22."""
    raw = u24(b, i)
    value = raw & 0x1FFFFF
    if value & 0x100000:
        value -= 0x200000
    return value, (raw >> 21) & 3


def decode(data):
    seq, n, count = data[0], data[1], data[2]
    samples = [fifo_sample(data, 3 + 3 * k) for k in range(n)]
    return seq, count, samples


async def read_regs(client, reg, n):
    await client.write_gatt_char(PPG_CHAR, bytes([0xFE, reg, n]), response=True)
    await asyncio.sleep(0.3)  # the ring holds its notifications for 1 s
    value = await client.read_gatt_char(PPG_CHAR)
    if len(value) != 3 + n or value[0] != 0xFE or value[1] != reg:
        return None
    return bytes(value[3:])


def dominant_bpm(x, fs=FS):
    """Strongest frequency between 0.7 and 3.5 Hz (42-210 bpm), by a plain DFT."""
    n = len(x)
    if n < 4 * fs:
        return None
    mean = sum(x) / n
    x = [v - mean for v in x]
    best = (0.0, None)
    f = 0.7
    while f <= 3.5:
        s = sum(v * cmath.exp(-2j * math.pi * f * k / fs) for k, v in enumerate(x))
        best = max(best, (abs(s), f), key=lambda t: t[0])
        f += 0.02
    return best[1] * 60 if best[1] else None


async def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("address")
    p.add_argument("--time", type=float, default=20)
    p.add_argument("--write", action="append", default=[], help="REG=VAL (hex), after the start")
    p.add_argument("--read", action="append", default=[], help="REG,N (hex,dec): print N registers")
    p.add_argument("--csv", help="write the FIFO samples (and phase registers) to this file")
    p.add_argument("--quiet", action="store_true", help="only the summary")
    args = p.parse_args()

    packets, fifo, counts = [], [], []
    t0 = None

    def on_data(_, data):
        nonlocal t0
        now = time.monotonic()
        t0 = t0 or now
        if data[0] == 0xFE and len(data) >= 3 and len(data) == 3 + data[2]:
            return  # register read result (characteristic value), not a packet
        seq, count, samples = decode(bytes(data))
        packets.append((now - t0, seq, count, samples))
        fifo.extend(samples)
        counts.append(count)
        if not args.quiet:
            print(f"{now - t0:6.2f}s seq {seq:3d}  count {count:2d}  "
                  f"fifo {' '.join(f'{v}/{f}' for v, f in samples)}")

    for attempt in range(5):
        dev = await BleakScanner.find_device_by_address(args.address, timeout=15)
        if not dev:
            print("not found")
            continue
        try:
            async with BleakClient(dev, timeout=20) as client:
                await client.start_notify(PPG_CHAR, on_data)  # starts the sensor
                for w in args.write:
                    reg, val = (int(v, 16) for v in w.split("="))
                    await client.write_gatt_char(PPG_CHAR, bytes([reg, val]), response=True)
                await asyncio.sleep(args.time)
                for r in args.read:
                    reg, n = r.split(",")
                    reg, n = int(reg, 16), int(n)
                    values = await read_regs(client, reg, n)
                    print(f"registers 0x{reg:02x}..: " + (values.hex(" ") if values else "read failed"))
                await client.stop_notify(PPG_CHAR)
            break
        except Exception as e:
            print("connection problem", attempt, repr(e))
            await asyncio.sleep(2)
    else:
        sys.exit("failed")

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["t", "seq", "fifo_count", "fifo_samples"])
            for t, seq, count, samples in packets:
                w.writerow([f"{t:.3f}", seq, count, " ".join(str(v) for v, _ in samples)])

    if not packets:
        sys.exit("no notification received")
    duration = packets[-1][0] or 1
    lost = sum((b[1] - a[1]) % 256 - 1 for a, b in zip(packets, packets[1:]))
    print(f"\n{len(packets)} packets in {duration:.1f} s ({len(packets) / duration:.1f}/s), "
          f"{lost} lost; {len(fifo)} FIFO samples ({len(fifo) / duration:.1f} Hz, expected {FS:g})")
    print(f"  FIFO count register: min {min(counts)}  max {max(counts)}")
    # The FIFO interleaves the phases (flag = phase index): analyse each one
    for flag in sorted({f for _, f in fifo}):
        x = [v for v, f in fifo if f == flag]
        mean = sum(x) / len(x)
        std = (sum((v - mean) ** 2 for v in x) / len(x)) ** 0.5
        print(f"  phase {flag}: {len(x) / duration:5.1f} Hz  min {min(x):8d}  max {max(x):8d}  "
              f"mean {mean:10.0f}  std {std:8.0f}")
        bpm = dominant_bpm(x)
        if bpm:
            print(f"           dominant frequency in 42-210 bpm: {bpm:.0f} bpm "
                  "(meaningful only with the ring worn)")


asyncio.run(main())
