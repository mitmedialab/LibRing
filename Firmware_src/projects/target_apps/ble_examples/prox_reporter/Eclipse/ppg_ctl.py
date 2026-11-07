#!/usr/bin/env python3
"""Interactive control of the PPG (heart-rate sensor) of a ring running the ppg branch.

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
  gpio PORT PIN MODE         GPIO test, modes: pd pu in low high restore  e.g. gpio 3 0 high
  gpioscan [S]               for each test pin: check that nothing drives it (pull-up/down),
                             then toggle it high/low (S s each, twice) while you watch the LEDs
  q                          quit
Requires: pip install bleak
"""
import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

PPG = "e7da0718-b8b6-44b6-912c-e0bfb05d562d"

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
    if len(d) < 3 or len(d) != 3 + 3 * d[1]:
        return  # register or GPIO read result (BlueZ reports reads as notifications too)
    count_reg = d[2]
    now = time.monotonic()
    for k in range(d[1]):
        raw = d[3 + 3 * k] | d[4 + 3 * k] << 8 | d[5 + 3 * k] << 16
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


GPIO_MODES = {"pd": 0, "pu": 1, "in": 2, "low": 3, "high": 4, "restore": 5}
# pins accepted by the firmware (gpio_test_pins in user_proxr.c): (port, pin, may drive)
GPIO_PINS = [(3, 0, True), (1, 4, True), (0, 4, True), (0, 7, False), (0, 1, False)]


async def gpio(client, port, pin, mode):
    await client.write_gatt_char(PPG, bytes([0xFD, port, pin, GPIO_MODES[mode]]), response=True)
    await asyncio.sleep(0.3)
    v = bytes(await client.read_gatt_char(PPG))
    if len(v) != 5 or v[0] != 0xFD or v[4] == 0xFF:
        raise RuntimeError(f"P{port}_{pin} {mode}: refused by the firmware")
    return v[4]


async def gpioscan(client, secs):
    for port, pin, drive in GPIO_PINS:
        name = f"P{port}_{pin}"
        pd = await gpio(client, port, pin, "pd")
        pu = await gpio(client, port, pin, "pu")
        fl = await gpio(client, port, pin, "in")
        follows = pd == 0 and pu == 1
        print(f"{name}: pull-down -> {pd}, pull-up -> {pu}, floating -> {fl}: "
              + ("not driven by anything" if follows else f"held at {pd} by the board"), flush=True)
        if follows and drive:
            for _ in range(2):
                for level in ("high", "low"):
                    await gpio(client, port, pin, level)
                    print(f"  {name} {level.upper():4s} now - look at the ring", flush=True)
                    await asyncio.sleep(secs)
                    print(f"     {stats(secs * 0.8)}", flush=True)
        await gpio(client, port, pin, "restore")


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
                elif cmd == "gpio":
                    level = await gpio(client, int(args[0]), int(args[1]), args[2])
                    print(f"P{args[0]}_{args[1]} {args[2]}: reads {level}")
                elif cmd == "gpioscan":
                    await gpioscan(client, float(args[0]) if args else 3.0)
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
