"""用第 17 章的半米样点生成 80 米结业场景。

在仓库根目录直接运行。保留原 48 米地形，在外圈添加林径和岬角。
"""

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Assets/Scenes/ch17/porch.json"
OUTPUT = ROOT / "Assets/Scenes/ch21/tour.json"
OLD_RADIUS = 24
RADIUS = 40
SPACING = 0.5
SIDE = round(2 * RADIUS / SPACING) + 1

FOREST = [[-9, 34], [-6.2, 29], [-3, 23.5]]
CAPE = [[16.3, -16.5], [16.5, -23.5], [18.8, -28.5], [23.2, -31.8], [29.5, -31.0]]


def smooth(low, high, value):
    """在两端导数为零的过渡，避免旧边界外立即出现陡坎。"""
    t = min(1.0, max(0.0, (value - low) / (high - low)))
    return t * t * (3.0 - 2.0 * t)


def expand(x, z, read_old):
    """旧区域按原格点复制，外圈的高度由坡形和离边距离共同控制。"""
    edge_x = min(OLD_RADIUS, max(-OLD_RADIUS, x))
    edge_z = min(OLD_RADIUS, max(-OLD_RADIUS, z))
    old = read_old(edge_x, edge_z)
    reach = max(abs(x - edge_x), abs(z - edge_z))
    if not reach:
        return old

    ripple = 0.18 * math.sin(x * 0.48 + z * 0.36) + 0.16 * math.cos(z * 0.61)
    forest_hill = 1.8 * math.exp(-((x + 17) / 9) ** 2 - ((z - 31) / 8) ** 2)
    stream = math.exp(-((z + 0.75 * math.sin(0.22 * x)) / 2.15) ** 2)
    west_land = 1.38 + ripple + forest_hill - 2.7 * stream
    offshore = smooth(17, 23, x)
    apron = west_land * (1 - offshore) + (-3.6 + ripple) * offshore

    # 贝壳形岬角从北岸向东延伸。外轮廓仍渐降成海床。
    d = math.hypot((x - 21.4) / 17.2, (z + 31.2) / 8.4)
    headland = 4.25 * (1.0 - smooth(0.57, 1.15, d))
    target = apron + headland * offshore + 1.2 * (1.0 - smooth(0.45, 1.2, d))
    blend = smooth(0, 4.5, reach)
    return round(old * (1 - blend) + target * blend, 4)


def write_scene():
    """编排一条从林地到海岬的主路，再沿路放置地标。"""
    scene = json.loads(SOURCE.read_text(encoding="utf-8"))
    old = scene["heightfield"]
    old_side, old_samples = old["samples_per_axis"], old["heights_m"]

    def read_old(x, z):
        col = round((x + OLD_RADIUS) / SPACING)
        row = round((z + OLD_RADIUS) / SPACING)
        return old_samples[row * old_side + col]

    def height(x, z):
        return expand(x, z, read_old)

    rows = [[height(col * SPACING - RADIUS, row * SPACING - RADIUS)
             for col in range(SIDE)] for row in range(SIDE)]
    old.update(samples_per_axis=SIDE, heights_m=[])
    scene.update(version=9, time_hours=12, spawn={"world_xz": FOREST[0], "yaw_deg": 151.0})
    scene["camera"] = {"target": [-6.5, 1.9, 27], "azimuth_deg": -20,
                       "elevation_deg": 19, "distance_m": 15}
    scene["road"]["control_xz"] = FOREST + scene["road"]["control_xz"][:-1] + CAPE

    def place(asset, x, z, yaw, scale=1):
        scene["props"].append({"asset": asset, "position": [x, height(x, z), z],
                               "rotation_deg": [0, yaw, 0], "scale": [scale] * 3})

    place("models/ch13/house.glb", -12, 31, 29, 0.79)
    place("models/ch13/fence.glb", -10.5, 33.5, 18)
    place("tree", -13.5, 35, 10, 1.25)
    place("tree", -10.5, 27.5, 0, 1.15)
    place("models/ch13/house.glb", 25, -34.5, 5, 0.78)
    place("models/ch13/fence.glb", 29.5, -32.5, 55)
    place("models/ch13/fence.glb", 30.5, -30.5, 98)
    place("rock", 27, -28.5, 0, 1.15)
    # 高度表每行是一个 Z 样点，其余场景字段保留结构化缩进。
    matrix = ",\n".join("      " + ", ".join(map(str, row)) for row in rows)
    content = json.dumps(scene, ensure_ascii=False, indent=2)
    content = content.replace('"heights_m": []', '"heights_m": [\n' + matrix + '\n    ]', 1)
    OUTPUT.write_text(content + "\n", encoding="utf-8")


if __name__ == "__main__":
    write_scene()
