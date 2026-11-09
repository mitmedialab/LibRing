#!/usr/bin/env python3
"""Interactive control of the PPG (heart-rate sensor) of a ring running our firmware.

  ppg_ctl.py ADDRESS

The sensor runs (stock heart-rate setup) while connected. Commands (hex values):
  w REG VAL [REG VAL ...]    write registers          e.g. w 20 88 1c 40
  r REG [N]                  read N registers (<= 16) e.g. r 1c 4
  dump FROM TO               read a register range    e.g. dump 16 3f
  preset NAME                apply a preset (list: preset)
  sweep REG FROM TO STEP S   write REG = FROM..TO every S seconds (watch the LEDs)
  stats [S]                  mean/std/min/max of each phase over the last S s (2)
  mon                        toggle a per-second status line
  restart                    stop + start the sensor (replays the stock setup)
  q                          quit
Requires: pip install bleak
"""
import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

PPG = "e7da0718-b8b6-44b6-912c-e0bfb05d562d"
HEADER = 35  # PPG_HEADER, ppg_read() in src/ppg/hx3605.h

# HX3918: phase 2 LED driver select 0x2b (bits 7:4), current 0x28 (0-255 = 0-200 mA)
PRESETS = {
    "green": {0x2b: 0x10, 0x28: 0x02},   # heart rate on a finger (LDR0)
    "red": {0x2b: 0x40, 0x28: 0x02},     # LDR2
    "ir": {0x2b: 0x20, 0x28: 0x02},      # LDR1 (visible only through a camera)
    "off": {0x28: 0x00},
    "visible": {0x18: 0x40, 0x19: 0x01, 0x1e: 0xff, 0x28: 0x30},  # 100 Hz, long pulses
}

samples = []  # (time, phase, value)
count_reg = 0


def on_data(_, data):
    global count_reg
    d = bytes(data)
    if len(d) < HEADER or len(d) != HEADER + 3 * d[1]:
        return  # register read result (BlueZ reports reads as notifications too)
    count_reg = d[2]
    now = time.monotonic()
    for k in range(d[1]):
        i = HEADER + 3 * k
        raw = d[i] | d[i + 1] << 8 | d[i + 2] << 16
        v = raw & 0x1FFFFF
        if v & 0x100000:
            v -= 0x200000
        samples.append((now, (raw >> 21) & 3, v))
    del samples[:-2000]


def stats(seconds=2.0):
    t_min = time.monotonic() - seconds
    out = []
    for ph in (0, 1):
        x = [v for t, f, v in samples if t >= t_min and f == ph]
        if not x:
            out.append(f"phase {ph}: no data")
            continue
        m = sum(x) / len(x)
        s = (sum((v - m) ** 2 for v in x) / len(x)) ** 0.5
        out.append(f"phase {ph}: mean {m:9.0f} std {s:7.0f} min {min(x):8d} max {max(x):8d} ({len(x) / seconds:.0f}/s)")
    return "   ".join(out) + f"   fifo {count_reg}"


async def write(client, regs):
    for r, v in regs:
        await client.write_gatt_char(PPG, bytes([r, v]), response=True)


async def read(client, reg, n):
    await client.write_gatt_char(PPG, bytes([0xFE, reg, n]), response=True)
    await asyncio.sleep(0.3)  # the ring holds its notifications for 1 s
    v = bytes(await client.read_gatt_char(PPG))
    if len(v) != 3 + n or v[0] != 0xFE or v[1] != reg:
        raise RuntimeError("unexpected read result")
    return v[3:]


async def monitor(flag):
    while True:
        await asyncio.sleep(1)
        if flag["on"]:
            print(stats(1.0), flush=True)


async def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    address = sys.argv[1]
    dev = None
    for _ in range(3):
        dev = await BleakScanner.find_device_by_address(address, timeout=15)
        if dev:
            break
    if not dev:
        sys.exit(f"{address} not found")
    async with BleakClient(dev, timeout=20) as client:
        await client.start_notify(PPG, on_data)
        print("connected, sensor started (stock heart-rate setup); 'help' for the commands, 'q' to quit")
        mon = {"on": False}
        mon_task = asyncio.create_task(monitor(mon))
        while True:
            try:
                line = (await asyncio.to_thread(input, "ppg> ")).split()
            except EOFError:
                break
            if not line:
                continue
            cmd, args = line[0], line[1:]
            try:
                if cmd == "q":
                    break
                elif cmd == "w":
                    vals = [int(a, 16) for a in args]
                    await write(client, list(zip(vals[::2], vals[1::2])))
                    print("ok")
                elif cmd == "r":
                    reg, n = int(args[0], 16), int(args[1]) if len(args) > 1 else 1
                    print(f"0x{reg:02x}: {(await read(client, reg, n)).hex(' ')}")
                elif cmd == "dump":
                    lo, hi = int(args[0], 16), int(args[1], 16)
                    for reg in range(lo, hi + 1, 16):
                        n = min(16, hi + 1 - reg)
                        print(f"0x{reg:02x}: {(await read(client, reg, n)).hex(' ')}")
                elif cmd == "preset":
                    if not args or args[0] not in PRESETS:
                        print("presets: " + ", ".join(f"{k} ({' '.join(f'{r:02x}={v:02x}' for r, v in p.items())})"
                                                      for k, p in PRESETS.items()))
                        continue
                    await write(client, PRESETS[args[0]].items())
                    print("ok")
                elif cmd == "sweep":
                    reg, lo, hi, step = (int(a, 16) for a in args[:4])
                    secs = float(args[4]) if len(args) > 4 else 1.0
                    v = lo
                    while (v <= hi) if lo <= hi else (v >= hi):
                        await write(client, [(reg, v)])
                        await asyncio.sleep(secs)
                        print(f"0x{reg:02x}={v:02x}  {stats(secs * 0.8)}", flush=True)
                        v += step if lo <= hi else -step
                elif cmd == "stats":
                    print(stats(float(args[0]) if args else 2.0))
                elif cmd == "mon":
                    mon["on"] = not mon["on"]
                elif cmd == "restart":
                    await client.stop_notify(PPG)
                    await asyncio.sleep(0.3)
                    await client.start_notify(PPG, on_data)
                    print("restarted")
                else:
                    print(__doc__)
            except (ValueError, IndexError):
                print("bad arguments, see: help")
            except Exception as e:
                print(f"error: {e}")
        mon_task.cancel()
        await client.stop_notify(PPG)


asyncio.run(main())
