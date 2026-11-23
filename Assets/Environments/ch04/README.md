# 第 04 章环境贴图

本章随附的 `golden-gate-hills.jpg` 来自 Poly Haven 的 **Golden Gate Hills** 360° 全景。Dimitrios Savva 摄影，Jarod Guest 后期处理。原始资源采用 CC0，可随课程再分发。 [原始全景与作者](https://polyhaven.com/a/golden_gate_hills) · [Poly Haven 资产许可](https://polyhaven.com/license)

课程版本把 4096 × 2048 的原始 HDR 全景压缩到 sRGB JPEG 的亮度范围，并水平循环平移，把云层与山脊放到默认行驶方向前方。天空球直接读取这张图，天空盒和车身使用从它重投影的六面 Cubemap。 [原始 4K Radiance HDR](https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/golden_gate_hills_4k.hdr)
