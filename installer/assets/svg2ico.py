#!/usr/bin/env python3
"""Convert lobster SVG to multi-resolution ICO."""

import io
import os
import struct
import cairosvg
from PIL import Image

SVG_FILE = os.path.join(os.path.dirname(__file__), "lobster.svg")
ICO_FILE = os.path.join(os.path.dirname(__file__), "lobster.ico")
PNG_FILE = os.path.join(os.path.dirname(__file__), "lobster.png")

SIZES = [16, 24, 32, 48, 64, 128, 256]


def svg_to_png_bytes(svg_path, size):
    """Render SVG to PNG at given size, return bytes."""
    png_data = cairosvg.svg2png(
        url=svg_path,
        output_width=size,
        output_height=size,
        background_color="transparent",
    )
    return png_data


def png_to_bmp(png_data):
    """Decode PNG to raw BGRA pixel array + size, return (w, h, rgba_bytes)."""
    img = Image.open(io.BytesIO(png_data))
    img = img.convert("RGBA")
    return img.size[0], img.size[1], img.tobytes()


def create_ico(sizes, svg_path, ico_path, png_path):
    """Create multi-res ICO from SVG."""
    # Save 256x256 PNG for title bar / installer use
    png256 = svg_to_png_bytes(svg_path, 256)
    with open(png_path, "wb") as f:
        f.write(png256)
    print(f"  Saved {png_path}")

    # Build ICO file
    images = []
    for size in sizes:
        png_data = svg_to_png_bytes(svg_path, size)
        w, h, rgba = png_to_bmp(png_data)
        # BMP needs flipped row order, 32-bit BGRA
        bmp_data = bytearray()
        row_size = w * 4
        for y in range(h - 1, -1, -1):
            row = rgba[y * row_size:(y + 1) * row_size]
            # RGBA -> BGRA
            for i in range(0, len(row), 4):
                bmp_data.append(row[i + 2])  # B
                bmp_data.append(row[i + 1])  # G
                bmp_data.append(row[i])      # R
                bmp_data.append(row[i + 3])  # A
        images.append((w if w < 256 else 0, h if h < 256 else 0, bytes(bmp_data)))
        print(f"  Rendered {size}x{size}")

    # Write ICO
    with open(ico_path, "wb") as f:
        # ICO header
        f.write(struct.pack("<HHH", 0, 1, len(images)))
        offset = 6 + 16 * len(images)
        for w, h, bmp in images:
            img_size = len(bmp) + 40  # 40 = BITMAPINFOHEADER
            # Entry
            f.write(struct.pack("<BBBBHHII",
                w, h, 0, 0, 1, 32, img_size, offset))
            offset += img_size
        for w, h, bmp in images:
            # BITMAPINFOHEADER
            xor_size = len(bmp)
            f.write(struct.pack("<IiiHHIIiiII",
                40, w, h * 2, 1, 32, 0, xor_size, 0, 0, 0, 0))
            f.write(bmp)
    print(f"  Saved {ico_path} ({os.path.getsize(ico_path)} bytes)")


if __name__ == "__main__":
    create_ico(SIZES, SVG_FILE, ICO_FILE, PNG_FILE)
    print("Done.")
