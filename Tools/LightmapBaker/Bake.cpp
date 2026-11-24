#include "Tools/LightmapBaker/Bake.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace tinyworld {
namespace {

using Triangle = std::array<GltfVertex, 3>;
constexpr int kSide = kPorchLightmapSide;
constexpr float kRayBias = 0.003f; // 模型空间，米
constexpr glm::vec3 kLamp{-0.44f, 2.15f, 0.55f};
constexpr glm::vec3 kLampPower{6.3f, 3.7f, 1.65f};

//————————————————————————————————————————————————————————————————————————————————
// 保留线性颜色和来源，填充阶段用来源区分无光表面和空白像素。
//————————————————————————————————————————————————————————————————————————————————
struct Texel {
    glm::vec3 light{0.0f};
    std::uint8_t mask{}; // 0 空白，128 扩边，255 三角形覆盖
};

//————————————————————————————————————————————————————————————————————————————————
// 返回二维有向面积，用于纹素内插和判断。
//————————————————————————————————————————————————————————————————————————————————
float Cross(glm::vec2 a, glm::vec2 b) {
    return a.x * b.y - a.y * b.x;
}

//————————————————————————————————————————————————————————————————————————————————
// 双面求交，候选交点还需位于表面和灯之间。
//————————————————————————————————————————————————————————————————————————————————
bool BlocksLight(const Triangle& triangle, glm::vec3 origin, glm::vec3 ray, float distance) {
    const glm::vec3 a = glm::make_vec3(triangle[0].geometry.position.data());
    const glm::vec3 e1 = glm::make_vec3(triangle[1].geometry.position.data()) - a;
    const glm::vec3 e2 = glm::make_vec3(triangle[2].geometry.position.data()) - a;
    const glm::vec3 p = glm::cross(ray, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1.0e-7f) return false;
    const glm::vec3 s = origin - a;
    const float u = glm::dot(s, p) / det;
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(ray, q) / det;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = glm::dot(e2, q) / det;
    return t > 0.0001f && t < distance;
}

//————————————————————————————————————————————————————————————————————————————————
// 计算给定表面点接收的灯光，背面和遮挡处返回零。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 Evaluate(glm::vec3 p, glm::vec3 n, const std::vector<Triangle>& triangles) {
    const glm::vec3 toLight = kLamp - (p + n * kRayBias);
    const float distance = glm::length(toLight);
    if (distance <= kRayBias) return {};
    const glm::vec3 ray = toLight / distance;
    const float facing = glm::dot(n, ray);
    if (facing <= 0.0f) return {};
    const auto blocked = [&](const auto& triangle) { return BlocksLight(triangle, p + n * kRayBias, ray, distance); };
    if (std::any_of(triangles.begin(), triangles.end(), blocked)) return {};
    return kLampPower * (facing / std::max(distance * distance, 0.08f));
}

//————————————————————————————————————————————————————————————————————————————————
// 遍历单片三角形的 UV 包围盒，从纹素中心重建空间点和法线。
//————————————————————————————————————————————————————————————————————————————————
void Rasterize(const Triangle& triangle, const std::vector<Triangle>& triangles, std::vector<Texel>& atlas) {
    const auto& [a, b, c] = triangle;
    const glm::vec2 start = a.lightmapUv * float(kSide);
    const glm::vec2 endB = b.lightmapUv * float(kSide);
    const glm::vec2 endC = c.lightmapUv * float(kSide);
    const auto e1 = endB - start;
    const auto e2 = endC - start;
    const float area = Cross(e1, e2);
    if (std::abs(area) < 1.0e-6f) throw std::runtime_error("Degenerate lightmap UV triangle.");
    const auto low = glm::ivec2(glm::floor(glm::min(start, glm::min(endB, endC))));
    const auto high = glm::min(glm::ivec2(glm::ceil(glm::max(start, glm::max(endB, endC)))), glm::ivec2(kSide - 1));
    for (int y = low.y; y <= high.y; ++y) {
        for (int x = low.x; x <= high.x; ++x) {
            const glm::vec2 d = glm::vec2(x + 0.5f, y + 0.5f) - start;
            const float u = Cross(d, e2) / area;
            const float v = Cross(e1, d) / area;
            const float w = 1.0f - u - v;
            if (std::min({u, v, w}) < -1.0e-6f) continue;
            auto& texel = atlas[y * kSide + x];
            // 两片共边时允许第二片命中同一中心，保留第一次计算。
            if (texel.mask) {
                if (std::min({u, v, w}) > 1.0e-6f) throw std::runtime_error("Overlapping lightmap UVs.");
                continue;
            }
            const glm::vec3 p = glm::make_vec3(a.geometry.position.data()) * w + glm::make_vec3(b.geometry.position.data()) * u + glm::make_vec3(c.geometry.position.data()) * v;
            const glm::vec3 n = glm::make_vec3(a.geometry.normal.data()) * w + glm::make_vec3(b.geometry.normal.data()) * u + glm::make_vec3(c.geometry.normal.data()) * v;
            if (glm::dot(n, n) < 1.0e-8f) throw std::runtime_error("Vanishing interpolated lightmap normal.");
            texel = {Evaluate(p, glm::normalize(n), triangles), 255};
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 每轮只读前一轮，黑色覆盖纹素同样可以为扩边提供颜色。
//————————————————————————————————————————————————————————————————————————————————
void Pad(std::vector<Texel>& atlas) {
    constexpr std::array<glm::ivec2, 8> neighbors{
        glm::ivec2{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
    };
    for (int ring = 0; ring < 3; ++ring) {
        const auto source = atlas;
        for (int y = 0; y < kSide; ++y) {
            for (int x = 0; x < kSide; ++x) {
                auto& texel = atlas[y * kSide + x];
                if (texel.mask) continue;
                for (const auto offset : neighbors) {
                    const int dx = x + offset.x, dy = y + offset.y;
                    if (dx < 0 || dy < 0 || dx >= kSide || dy >= kSide) continue;
                    const auto& donor = source[dy * kSide + dx];
                    if (donor.mask) {
                        texel = {donor.light, 128};
                        break;
                    }
                }
            }
        }
    }
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 展开遮挡几何，烘焙有效图岛，最后量化为线性 RGBA8。
//————————————————————————————————————————————————————————————————————————————————
GltfImage BakePorch(const GltfAsset& asset) {
    std::vector<Triangle> triangles;
    for (const auto& primitive : asset.primitives) {
        if (!primitive.hasLightmapUv) throw std::runtime_error("The porch requires TEXCOORD_1.");
        for (const auto& vertex : primitive.vertices) {
            if (glm::any(glm::lessThan(vertex.lightmapUv, glm::vec2(0.0f))) ||
                glm::any(glm::greaterThan(vertex.lightmapUv, glm::vec2(1.0f)))) {
                throw std::runtime_error("Lightmap UV lies outside [0, 1].");
            }
        }
        for (std::size_t i = 0; i < primitive.vertices.size(); i += 3) {
            triangles.push_back({primitive.vertices[i], primitive.vertices[i + 1], primitive.vertices[i + 2]});
        }
    }
    std::vector<Texel> atlas(kSide * kSide);
    for (const auto& triangle : triangles) Rasterize(triangle, triangles, atlas);
    Pad(atlas);
    GltfImage result{kSide, kSide, std::vector<std::uint8_t>(atlas.size() * 4)};
    for (std::size_t i = 0; i < atlas.size(); ++i) {
        const auto encoded = glm::round(glm::clamp(atlas[i].light / kLightmapRange, 0.0f, 1.0f) * 255.0f);
        for (int channel = 0; channel < 3; ++channel) result.pixels[4 * i + channel] = std::uint8_t(encoded[channel]);
        result.pixels[4 * i + 3] = atlas[i].mask;
    }
    return result;
}

}  // namespace tinyworld
