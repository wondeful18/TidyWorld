"""从第 13 章村庄存档重建第 15 章的河口、岸坡与步行路线。"""

import argparse
import json
import math
from pathlib import Path

WATER_Y = -0.38


def smoothstep(low, high, value):
    """在两个阈值之间用三次曲线平滑过渡。"""
    t = max(0.0, min(1.0, (value - low) / (high - low)))
    return t * t * (3.0 - 2.0 * t)


def mix(left, right, weight):
    """在线性权重下混合两个高度。"""
    return left + (right - left) * weight


def coast_height(x, z, previous_y):
    """保留西边的村庄，在东边生成沙缓坡、岩岸和贯通湖盆的河口。"""
    coastline_x = (19.5 + 1.8 * math.sin(0.23 * z + 0.2)
                   + 0.95 * math.cos(0.42 * z)
                   + 4.9 * math.exp(-((z + 10.5) / 4.5) ** 2))
    rock = 1.0 - smoothstep(-7.0, 4.0, z)
    slope = mix(0.32, 0.73, rock)
    shore_y = WATER_Y + max(-4.0, min(3.4, (coastline_x - x) * slope))
    terrain = mix(previous_y, shore_y, smoothstep(12.5, 20.5, x))

    mouth_z = -2.6 + 0.31 * (x - 14.0) + 0.48 * math.sin((x - 13.0) * 0.45)
    mouth_width = mix(1.35, 3.25, smoothstep(15.0, 25.0, x))
    channel = 1.0 - smoothstep(0.60, 1.55, abs(z - mouth_z) / mouth_width)
    excavation = channel * smoothstep(12.5, 16.8, x)
    channel_y = WATER_Y - mix(0.48, 1.22, smoothstep(16.0, 24.0, x))
    estuary = mix(terrain, min(terrain, channel_y), excavation)
    # 在样区东缘降低海床，让可见岸坡在地形结束前充分没入海里。
    shelf = smoothstep(21.8, 24.0, x)
    return round(mix(estuary, min(estuary, WATER_Y - 3.4), shelf), 4)


def make_scene(scene):
    """更新高度样本和同一条道路，保留桥、出生点和村庄资产实例。"""
    field = scene["heightfield"]
    count = field["samples_per_axis"]
    step = field["spacing_m"]
    offset = (count - 1) * step / 2.0
    heights = field["heights_m"]
    for row in range(count):
        for column in range(count):
            index = row * count + column
            x, z = column * step - offset, row * step - offset
            heights[index] = coast_height(x, z, heights[index])
    scene["road"]["control_xz"] = [
        [-1, 19], [0, 11], [0, 5.6], [0, 0], [0, -5.6],
        [0.9, -9.2], [4.6, -11.5], [10.5, -12.3], [15.6, -11.8], [21.1, -10.4],
    ]
    scene["road"]["width_m"] = 1.55
    scene["camera"] = {
        "target": [6.0, -0.50, 0.0],
        "azimuth_deg": -89.0, "elevation_deg": 27.0, "distance_m": 42.0,
    }
    scene["vegetation"]["min_plant_y_m"] = 0.04
    # 海岬的围栏标记路线终点，Y 与世界地形样点对齐。
    x, z = 21.5, -10.5
    ix, iz = round((x + offset) / step), round((z + offset) / step)
    scene["props"].append({
        "asset": "models/ch13/fence.glb", "position": [x, heights[iz * count + ix], z],
        "rotation_deg": [0, 80, 0], "scale": [0.7, 1, 1],
    })
    return scene


def main():
    """写出每行对应一排高度样点的场景 JSON，便于在 Git 中检查地形变化。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("village", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    scene = make_scene(json.loads(args.village.read_text(encoding="utf-8")))
    field = scene["heightfield"]
    heights = field["heights_m"]
    count = field["samples_per_axis"]
    rows = [heights[i:i + count] for i in range(0, len(heights), count)]
    field["heights_m"] = "__HEIGHT_ROWS__"
    serialized = json.dumps(scene, ensure_ascii=False, indent=2)
    lines = ",\n".join("      " + ", ".join(map(str, row)) for row in rows)
    args.output.write_text(
        serialized.replace('"__HEIGHT_ROWS__"', "[\n" + lines + "\n    ]") + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
