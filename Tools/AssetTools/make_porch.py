"""为第 17 章生成带独立烘焙 UV 图岛的木门廊 GLB。"""

import json
import math
from pathlib import Path
import struct
import sys


class Porch:
    """按材质分组组装几何，并为每个平面预留一块图岛。"""

    def __init__(self):
        self.parts = []
        self.chart = 0
        self.materials = []

    def material(self, name, color, roughness):
        """添加一份只用常量底色的 glTF 材质。"""
        self.materials.append({
            "name": name, "pbrMetallicRoughness":
            {"baseColorFactor": [*color, 1], "metallicFactor": 0,
             "roughnessFactor": roughness},
        })
        self.parts.append([])
        return len(self.parts) - 1

    def face(self, vertices, material, large=None):
        """四边形拆为两片，保留连续 UV，并为不同平面分配不同图岛。"""
        a, b, c, _ = vertices
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        cross = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        n = [value / math.hypot(*cross) for value in cross]
        if large is None:
            x = 4 + 16 * (self.chart % 16)
            y = 93 + 16 * (self.chart // 16)
            assert y + 8 < 256
            self.chart += 1
            large = (x, y, 8, 8)
        x, y, w, h = large
        corners = [(x, y+h), (x+w, y+h), (x+w, y), (x, y)]
        primary = [(0, 1), (1, 1), (1, 0), (0, 0)]
        for i in (0, 1, 2, 0, 2, 3):
            self.parts[material].append((vertices[i], n, primary[i],
                                         [axis / 256 for axis in corners[i]]))

    def box(self, low, high, material, large_faces=None):
        """以从外侧可见的绕序写入盒子的六面。"""
        x, y, z = low
        X, Y, Z = high
        faces = (
            ((x,y,Z),(X,y,Z),(X,Y,Z),(x,Y,Z)),
            ((X,y,z),(x,y,z),(x,Y,z),(X,Y,z)),
            ((X,y,Z),(X,y,z),(X,Y,z),(X,Y,Z)),
            ((x,y,z),(x,y,Z),(x,Y,Z),(x,Y,z)),
            ((x,Y,Z),(X,Y,Z),(X,Y,z),(x,Y,z)),
            ((x,y,z),(X,y,z),(X,y,Z),(x,y,Z)),
        )
        for slot, vertices in enumerate(faces):
            self.face(vertices, material, (large_faces or {}).get(slot))

    def write(self, path):
        """写无索引的三角形列表，顶点属性分别存进四个 accessor。"""
        data = bytearray()
        views = []
        accessors = []
        prims = []
        for material, vertices in enumerate(self.parts):
            attributes = {}
            for slot, (name, width) in enumerate((
                    ("POSITION", 3), ("NORMAL", 3), ("TEXCOORD_0", 2), ("TEXCOORD_1", 2))):
                values = [v[slot] for v in vertices]
                raw = b"".join(struct.pack(f"<{width}f", *v) for v in values)
                views.append(dict(buffer=0, byteOffset=len(data), byteLength=len(raw), target=34962))
                accessor = dict(bufferView=len(views)-1, componentType=5126,
                                count=len(values), type=f"VEC{width}")
                if name == "POSITION":
                    accessor["min"] = [min(axis) for axis in zip(*values)]
                    accessor["max"] = [max(axis) for axis in zip(*values)]
                attributes[name] = len(accessors)
                accessors.append(accessor)
                data.extend(raw)
            prims.append(dict(attributes=attributes, material=material))
        document = dict(asset=dict(version="2.0", generator="TinyWorld porch"),
                        scene=0, scenes=[dict(nodes=[0])], nodes=[dict(mesh=0, name="Workshop porch")],
                        meshes=[dict(primitives=prims)], materials=self.materials,
                        bufferViews=views, accessors=accessors, buffers=[dict(byteLength=len(data))])
        manifest = json.dumps(document, separators=(",", ":")).encode()
        manifest += b" " * (-len(manifest) % 4)
        content = (struct.pack("<4sII", b"glTF", 2, 28 + len(manifest) + len(data))
                   + struct.pack("<I4s", len(manifest), b"JSON") + manifest
                   + struct.pack("<I4s", len(data), b"BIN\x00") + data)
        path.write_bytes(content)


def main():
    """构造灯、立柱、地板和浅色外墙。大图岛分别覆盖地板、墙与顶棚。"""
    p = Porch()
    plaster = p.material("Warm plaster", (.73, .61, .43), .87)
    timber = p.material("Oiled pine floor", (.50, .34, .20), .80)
    trim = p.material("Petrol blue painted posts", (.065, .16, .18), .81)
    roof = p.material("Terracotta roof", (.38, .102, .055), .78)
    dark = p.material("Door and lamp fitting", (.115, .065, .033), .85)
    glass = p.material("Opal lamp diffuser", (.88, .82, .70), .8)
    p.box((-1.92,.12,-2.55),(1.92,2.68,0),plaster,{0:(111,6,99,76)})
    p.box((-1.99,-.34,-2.58),(1.99,.13,.05),dark)
    p.box((-2.23,-.20,-.08),(2.23,.18,1.90),timber,{4:(5,6,98,76)})
    p.box((-1.62,-.24,1.90),(1.62,.07,2.30),timber)
    p.box((-2.13,2.48,-.15),(2.13,2.63,1.76),timber,{5:(218,6,32,76)})
    p.box((-2.24,2.63,-.16),(2.24,2.82,1.84),roof)
    # 檐口宽梁、两侧柱，以及偏心的遮挡柱。
    p.box((-2.21,2.38,1.63),(2.21,2.63,1.80),trim)
    for x in (-1.88, 1.89):
        p.box((x-.10,.05,1.59),(x+.10,2.51,1.79),trim)
    p.box((.28,.18,.94),(.51,2.50,1.17),trim)
    p.box((-1.34,.18,0),(-.28,2.04,.085),trim)
    p.box((-1.22,.18,.085),(-.40,1.89,.125),dark)
    p.box((.72,.95,0),(1.52,1.83,.07),trim)
    p.box((.79,1.02,.07),(1.45,1.76,.10),dark)
    p.box((-.59,2.26,.40),(-.29,2.49,.70),dark)
    p.box((-.56,2.19,.43),(-.32,2.26,.67),glass)
    # 主屋的坡顶。屋面和两端山墙分别展开为图岛。
    p.face(((-2.12,2.58,-2.72),(-2.12,2.58,.21),(0,3.56,.21),(0,3.56,-2.72)),roof)
    p.face(((0,3.56,-2.72),(0,3.56,.21),(2.12,2.58,.21),(2.12,2.58,-2.72)),roof)
    # 山墙用窄顶的梯形填满，保留可见面的绕序和非退化图岛。
    p.face(((-1.91,2.66,.005),(1.91,2.66,.005),(.02,3.54,.005),(-.02,3.54,.005)),plaster)
    p.face(((1.91,2.66,-2.554),(-1.91,2.66,-2.554),(-.02,3.54,-2.554),(.02,3.54,-2.554)),plaster)
    p.write(Path(sys.argv[1]))


if __name__ == "__main__":
    main()
