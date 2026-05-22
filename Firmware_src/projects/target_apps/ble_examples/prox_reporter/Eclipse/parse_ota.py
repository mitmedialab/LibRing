#!/usr/bin/env python3
"""Parse and display the OTA image header (SR08 / DA14585 format).

Header layout (64 bytes, from make_bin_ota.py / ATC1441):
  [0-1]  signature   0x70 0x51
  [2]    image_id    0xAA
  [3]    valid_flag  0xFF
  [4-7]  all_size    total padded payload size (uint32 LE)
  [8-11] crc32       CRC32 of raw firmware bytes (uint32 LE)
  [12-27] version    ASCII string, NUL + 0xFF padded to 16 bytes
  [28-31] timestamp  Unix epoch (uint32 LE)
  [32]   enc_flag    0x00 = unencrypted
  [33-36] code_size  raw firmware byte count (uint32 LE)
  [37-40] custom     0x00170001
  [41-63] reserved   0xFF * 23
  [-1]   xor_check   XOR of every byte except the last
"""
import struct, sys, datetime

path = sys.argv[1] if len(sys.argv) > 1 else "output.bin"
data = open(path, "rb").read()

sig     = data[0:2]
img_id  = data[2]
valid   = data[3]
all_sz  = struct.unpack_from("<I", data, 4)[0]
crc32   = struct.unpack_from("<I", data, 8)[0]
version = data[12:28].rstrip(b"\xff\x00").decode("ascii", errors="replace")
ts      = struct.unpack_from("<I", data, 28)[0]
enc     = data[32]
code_sz = struct.unpack_from("<I", data, 33)[0]
custom  = struct.unpack_from("<I", data, 37)[0]
xor_chk = data[-1]

sig_ok  = sig == bytes([0x70, 0x51])
xor_ref = 0
for b in data[:-1]:
    xor_ref ^= b

print(f"OTA image : {path}  ({len(data):,} bytes total)")
print(f"  Signature : {'OK' if sig_ok else 'BAD'} ({sig.hex()})")
print(f"  Image ID  : 0x{img_id:02x}   valid=0x{valid:02x}")
print(f"  Code size : {code_sz:,} B  ({code_sz / 1024:.1f} KiB)  — raw firmware")
print(f"  OTA size  : {all_sz:,} B  ({all_sz / 1024:.1f} KiB)  — padded payload")
print(f"  CRC32     : 0x{crc32:08x}")
print(f"  Version   : {version!r}")
print(f"  Timestamp : {datetime.datetime.fromtimestamp(ts)}")
print(f"  Encrypted : {'yes' if enc else 'no'}")
print(f"  Custom    : 0x{custom:08x}")
print(f"  XOR check : {'OK' if xor_chk == xor_ref else f'BAD (file=0x{xor_chk:02x} expected=0x{xor_ref:02x})'}")
