#pragma once

#include "Engine/Scene/SceneMesh.h"

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// PNG/JPEG 解码后的 RGBA8 字节，色彩解释留给上传纹理时的格式。
//————————————————————————————————————————————————————————————————————————————————
struct GltfImage {
    std::uint32_t width{1};   // 宽度，单位为像素
    std::uint32_t height{1};  // 高度，单位为像素
    std::vector<std::uint8_t> pixels{255, 255, 255, 255};  // 缺图时使用白色
};

//————————————————————————————————————————————————————————————————————————————————
// glTF 金属度／粗糙度材质，纹理乘以各自的线性系数。
//————————————————————————————————————————————————————————————————————————————————
struct GltfMaterial {
    GltfImage baseColor;             // RGB 通道按 sRGB 解释
    GltfImage metalRough;            // B 为金属度，G 为粗糙度，均为线性数据
    glm::vec3 baseFactor{1.0f};      // 线性的底色乘数
    float metallicFactor{1.0f};      // 金属度乘数
    float roughnessFactor{1.0f};     // 粗糙度乘数
};

//————————————————————————————————————————————————————————————————————————————————
// 导入结果保留烘入节点变换的网格顶点，以及 glTF 的两套 UV。
//————————————————————————————————————————————————————————————————————————————————
struct GltfVertex {
    SceneVertex geometry;   // 资产空间的顶点属性
    glm::vec2 uv;           // 原始纹理坐标，原点位于左上
    glm::vec2 lightmapUv{0.0f};  // 第二套 UV，缺失时由批次标记区分
};

//————————————————————————————————————————————————————————————————————————————————
// 一组有索引三角形只引用一种材质，节点局部变换已合并到顶点。
//————————————————————————————————————————————————————————————————————————————————
struct GltfPrimitive {
    std::vector<GltfVertex> vertices;  // 每个引用到的访问器顶点只保存一次
    std::vector<std::uint32_t> indices; // 按绘制顺序排列的三角形顶点索引
    std::size_t material{};             // 对应资产内的材质索引
    bool hasLightmapUv{};              // 是否含有 TEXCOORD_1
};

//————————————————————————————————————————————————————————————————————————————————
// 可由多个场景实例共享的一份 CPU 原型。
//————————————————————————————————————————————————————————————————————————————————
struct GltfAsset {
    std::vector<GltfPrimitive> primitives;  // 默认场景的网格批次
    std::vector<GltfMaterial> materials;    // 批次可复用的材质
};

//————————————————————————————————————————————————————————————————————————————————
// 读取课程约定的静态 glTF 2.0 子集。缺失或不支持的数据会带资产路径报错。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] GltfAsset LoadGltfAsset(const std::filesystem::path& path);

}  // namespace tinyworld
