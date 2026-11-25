"""Embed a generated cedar normal map in a clean copy of the course bridge GLB."""

import argparse
import json
import math
import struct
import zlib
from pathlib import Path


def png_chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", zlib.crc32(kind + data)))


def cedar_normal_png(size=128):
    def height(u, v):
        # Grain runs along U, while a gentle wave varies its path along the board.
        grain = v + 0.032 * math.sin(math.tau * u)
        return 0.009 * math.sin(math.tau * 9 * grain) + 0.003 * math.sin(math.tau * 17 * grain)

    rows = bytearray()
    step = 1 / size
    for y in range(size):
        rows.append(0)  # PNG row without a predictor
        v = (y + 0.5) * step
        for x in range(size):
            u = (x + 0.5) * step
            du = (height(u + step, v) - height(u - step, v)) / (2 * step)
            dv = (height(u, v + step) - height(u, v - step)) / (2 * step)
            length = math.sqrt(du * du + dv * dv + 1)
            rows.extend(round(255 * (component / length * 0.5 + 0.5)) for component in (-du, -dv, 1))
    return (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
            + png_chunk(b"IDAT", zlib.compress(rows, level=9))
            + png_chunk(b"IEND", b""))


def embed_normal(source, output):
    original = source.read_bytes()
    json_size, json_kind = struct.unpack_from("<I4s", original, 12)
    bin_header = 20 + json_size
    bin_size, bin_kind = struct.unpack_from("<I4s", original, bin_header)
    if original[:8] != b"glTF\x02\0\0\0" or json_kind != b"JSON" or bin_kind != b"BIN\0":
        raise ValueError("Expected the course bridge in GLB 2.0 format.")
    gltf = json.loads(original[20:bin_header])
    material = next(item for item in gltf["materials"] if item["name"] == "Cedar decking")
    if "normalTexture" in material:
        raise ValueError("Use the original bridge GLB before embedding a normal map.")
    binary = original[bin_header + 8:bin_header + 8 + bin_size]
    image = cedar_normal_png()
    views = gltf["bufferViews"]
    images = gltf["images"]
    textures = gltf["textures"]
    material["normalTexture"] = {"index": len(textures), "scale": 1.15}
    textures.append({"source": len(images)})
    images.append({"bufferView": len(views), "mimeType": "image/png", "name": "Cedar tangent normals"})
    views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(image)})
    binary += image
    gltf["buffers"][0]["byteLength"] = len(binary)
    binary += bytes(-len(binary) % 4)
    metadata = json.dumps(gltf, separators=(",", ":")).encode()
    metadata += b" " * (-len(metadata) % 4)
    output.write_bytes(struct.pack("<4sII", b"glTF", 2, 28 + len(metadata) + len(binary))
                       + struct.pack("<I4s", len(metadata), b"JSON") + metadata
                       + struct.pack("<I4s", len(binary), b"BIN\0") + binary)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="original bridge GLB without a normal map")
    parser.add_argument("output", type=Path, help="bridge GLB with the new normal map")
    args = parser.parse_args()
    embed_normal(args.source, args.output)
