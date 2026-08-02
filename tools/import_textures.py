#!/usr/bin/env python3
"""Convert a PNG texture atlas into the game's per-tile N64 CI4 header.

Draw one 16x16 cell per texture, four cells to a row, in the order the tiles
are declared in generate_assets.py's PALETTES -- that order is the contract, so
adding a texture there means adding a cell here.  The atlas may be drawn at any
whole multiple of 16 and is downsampled; each cell is quantised to sixteen
colours independently, because each gets its own palette on the console.

Drop the result at art/custom-textures.png and the build picks it up
automatically, replacing the procedural tiles.  Delete it and the procedural
tiles come back.  See docs/custom-textures.md.

This importer uses only the Python standard library, so the N64 build container
does not need image-processing packages.
"""

from collections import Counter
from pathlib import Path
from struct import unpack
from sys import argv, path
from zlib import decompress

ROOT = Path(__file__).resolve().parents[1]
path.insert(0, str(ROOT))

from generate_assets import PALETTES, rgba5551 as generated_rgba5551, tile

OUTPUT = ROOT / "assets" / "texture_data.h"
TILE_SIZE = 16
ATLAS_COLUMNS = 4
# The generator owns the tile list; this follows it rather than restating it,
# so the two cannot disagree about what cell three is.
TILE_NAMES = tuple(PALETTES)
ATLAS_ROWS = (len(TILE_NAMES) + ATLAS_COLUMNS - 1) // ATLAS_COLUMNS


def paeth(left, up, up_left):
    prediction = left + up - up_left
    left_distance = abs(prediction - left)
    up_distance = abs(prediction - up)
    diagonal_distance = abs(prediction - up_left)
    if left_distance <= up_distance and left_distance <= diagonal_distance:
        return left
    if up_distance <= diagonal_distance:
        return up
    return up_left


def decode_png(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("texture source must be a PNG")

    offset = 8
    width = height = bit_depth = color_type = None
    compressed = bytearray()
    while offset < len(data):
        length = unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += length + 12
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = unpack(">IIBBBBB", payload)
            if bit_depth != 8 or color_type not in (2, 6) or compression or filtering or interlace:
                raise ValueError("PNG must be non-interlaced 8-bit RGB or RGBA")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break

    if width is None:
        raise ValueError("PNG is missing its header")
    channels = 4 if color_type == 6 else 3
    raw = decompress(compressed)
    stride = width * channels
    previous = [0] * stride
    rows = []
    cursor = 0
    for _ in range(height):
        filter_type = raw[cursor]
        cursor += 1
        encoded = raw[cursor:cursor + stride]
        cursor += stride
        row = [0] * stride
        for index, value in enumerate(encoded):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            up_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                row[index] = value
            elif filter_type == 1:
                row[index] = (value + left) & 0xFF
            elif filter_type == 2:
                row[index] = (value + up) & 0xFF
            elif filter_type == 3:
                row[index] = (value + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                row[index] = (value + paeth(left, up, up_left)) & 0xFF
            else:
                raise ValueError("unsupported PNG filter")
        rows.append(row)
        previous = row

    pixels = []
    for row in rows:
        pixels.append([tuple(row[index:index + 3]) for index in range(0, stride, channels)])
    return width, height, pixels


def median_cut(colours, max_colours):
    """Return a deterministic weighted palette of no more than max_colours."""
    buckets = [list(Counter(colours).items())]
    while len(buckets) < max_colours:
        eligible = [bucket for bucket in buckets if len(bucket) > 1]
        if not eligible:
            break
        bucket = max(
            eligible,
            key=lambda entries: max(max(colour[channel] for colour, _ in entries) - min(colour[channel] for colour, _ in entries) for channel in range(3)) * sum(count for _, count in entries),
        )
        buckets.remove(bucket)
        ranges = [max(colour[channel] for colour, _ in bucket) - min(colour[channel] for colour, _ in bucket) for channel in range(3)]
        channel = max(range(3), key=lambda index: ranges[index])
        bucket.sort(key=lambda entry: entry[0][channel])
        midpoint = sum(count for _, count in bucket) / 2
        running = 0
        split = 1
        for index, (_, count) in enumerate(bucket):
            running += count
            if running >= midpoint:
                # Keep both halves non-empty. An old unused atlas cell can
                # contain a dominant flat colour; now that it is the water
                # tile, palette reduction must handle that case too.
                split = min(len(bucket) - 1, max(1, index + 1))
                break
        buckets.extend((bucket[:split], bucket[split:]))

    palette = []
    for bucket in buckets:
        total = sum(count for _, count in bucket)
        palette.append(tuple(sum(colour[channel] * count for colour, count in bucket) // total for channel in range(3)))
    return palette


def rgba5551(colour):
    red, green, blue = colour
    return ((red >> 3) << 11) | ((green >> 3) << 6) | ((blue >> 3) << 1) | 1


def pack_nibbles(indices):
    words = []
    for start in range(0, len(indices), 8):
        word = 0
        for index in indices[start:start + 8]:
            word = (word << 4) | index
        words.append(word)
    return words


def convert_tile(pixels):
    palette = median_cut(pixels, 16)
    indices = [min(range(len(palette)), key=lambda index: sum((colour[channel] - palette[index][channel]) ** 2 for channel in range(3))) for colour in pixels]
    words = pack_nibbles(indices)
    palette_words = [rgba5551(colour) for colour in palette] + [0] * (16 - len(palette))
    return words, palette_words


def generated_tile_pixels(name):
    """The procedural tile generate_assets.py would have produced."""
    palette = PALETTES[name]
    return [
        palette[tile(name, x, y)]
        for y in range(TILE_SIZE) for x in range(TILE_SIZE)
    ]


def usable_tile_pixels(name, pixels):
    """An unpainted cell falls back to the procedural tile rather than to
    black.  That is what lets an atlas be filled in a few tiles at a time: the
    ones you have not drawn yet keep working instead of turning the surfaces
    that use them into holes."""
    if max(max(colour) for colour in pixels) < 40:
        return generated_tile_pixels(name)
    return pixels


def main():
    if len(argv) != 2:
        raise SystemExit("usage: import_textures.py <texture atlas.png>")
    width, height, image = decode_png(argv[1])
    if width % ATLAS_COLUMNS or height % ATLAS_ROWS:
        raise ValueError(
            "atlas must divide evenly into a %dx%d grid of cells"
            % (ATLAS_COLUMNS, ATLAS_ROWS))
    cell_width, cell_height = width // ATLAS_COLUMNS, height // ATLAS_ROWS
    if cell_width < TILE_SIZE or cell_height < TILE_SIZE:
        raise ValueError("each atlas cell must be at least 16x16")

    chunks = ["// Generated from art/custom-textures.png by tools/import_textures.py.\n",
              "// The Texture type itself lives in engine/include/gfx.h.\n",
              '#include "gfx.h"\n']
    for number, name in enumerate(TILE_NAMES):
        origin_x = (number % ATLAS_COLUMNS) * cell_width
        origin_y = (number // ATLAS_COLUMNS) * cell_height
        # Nearest sampling preserves authored pixel work rather than blurring it.
        tile_pixels = [
            image[origin_y + min(cell_height - 1, (y * cell_height + cell_height // 2) // TILE_SIZE)]
                 [origin_x + min(cell_width - 1, (x * cell_width + cell_width // 2) // TILE_SIZE)]
            for y in range(TILE_SIZE) for x in range(TILE_SIZE)
        ]
        tile_pixels = usable_tile_pixels(name, tile_pixels)
        words, palette = convert_tile(tile_pixels)
        chunks.append("\nTexture %s_texture = { { %s }, { %s } };\n" % (
            name,
            ", ".join("0x%08X" % word for word in words),
            ", ".join("0x%04X" % word for word in palette),
        ))
    # The atlas is deliberately just the twelve terrain textures. Preserve
    # generated UI/sky textures (sun and moon phases) that share this compact
    # C header without requiring artists to add them to the 4x3 source image.
    for name, palette in PALETTES.items():
        if name in TILE_NAMES:
            continue
        words = pack_nibbles([tile(name, x, y)
                              for y in range(TILE_SIZE)
                              for x in range(TILE_SIZE)])
        palette_words = [generated_rgba5551(colour) for colour in palette] + \
            [0] * (16 - len(palette))
        chunks.append("\nTexture %s_texture = { { %s }, { %s } };\n" % (
            name,
            ", ".join("0x%08X" % word for word in words),
            ", ".join("0x%04X" % word for word in palette_words),
        ))
    OUTPUT.parent.mkdir(exist_ok=True)
    OUTPUT.write_text("".join(chunks))


if __name__ == "__main__":
    main()
