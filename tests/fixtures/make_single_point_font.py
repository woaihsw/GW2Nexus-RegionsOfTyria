"""Generate a minimal test TTF with a single off-curve point for U+E001."""
from pathlib import Path
import struct


def u16(*values):
    return struct.pack(">" + "H" * len(values), *values)


def u32(*values):
    return struct.pack(">" + "I" * len(values), *values)


head = bytearray(54)
head[0:4] = u32(0x00010000)
head[18:20] = u16(1000)  # units per em
head[50:52] = u16(1)  # long loca offsets
hhea = bytearray(36)
hhea[0:4] = u32(0x00010000)
hhea[4:8] = struct.pack(">hh", 800, -200)
hhea[34:36] = u16(2)  # horizontal metrics for .notdef and the test glyph
# One contour, zero-area bounds, endpoint 0, no instructions, flags 0x30.
# Both coordinate deltas are zero; the sole point is off the curve.
glyph = u16(1, 0, 0, 0, 0, 0, 0) + bytes([0x30, 0])
tables = {
    b"head": bytes(head),
    b"hhea": bytes(hhea),
    b"hmtx": u16(500, 0, 500, 0),
    b"maxp": u32(0x00010000) + u16(2) + bytes(26),
    b"loca": u32(0, 0, len(glyph)),
    b"glyf": glyph,
    b"cmap": u16(0, 1, 3, 10) + u32(12)
        + u16(12, 0) + u32(28, 0, 1, 0xE001, 0xE001, 1),
}
count = len(tables)
power = 2 ** (count.bit_length() - 1)
header = u32(0x00010000) + u16(count, power * 16, power.bit_length() - 1, count * 16 - power * 16)
directory = bytearray()
data = bytearray()
offset = 12 + count * 16
for tag, table in sorted(tables.items()):
    padded = table + bytes(-len(table) % 4)
    checksum = sum(struct.unpack(">" + "I" * (len(padded) // 4), padded)) & 0xFFFFFFFF
    directory += tag + u32(checksum, offset, len(table))
    data += padded
    offset += len(padded)
Path(__file__).with_name("single_point.ttf").write_bytes(header + directory + data)
