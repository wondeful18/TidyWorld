#include "Engine/Terrain/RoadSpline.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 用外侧两点确定入弯、出弯趋势，在中间 p1 与 p2 之间插值。
//————————————————————————————————————————————————————————————————————————————————
glm::vec2 CatmullRom(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (p2 - p0) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

//————————————————————————————————————————————————————————————————————————————————
// 三角形裁过五条边界后最多有八个角。保留顺序以便三角扇展开。
//————————————————————————————————————————————————————————————————————————————————
struct Polygon {
    std::array<glm::vec2, 8> vertices{};  // 保留在裁剪区域内的 XZ 点
    std::size_t count{};                // 顶点数组中的有效项
};

//————————————————————————————————————————————————————————————————————————————————
// 用点在线性边界函数的正负号判定内外，翻号处插入线段交点。
//————————————————————————————————————————————————————————————————————————————————
Polygon Clip(const Polygon& input, glm::vec2 normal, float offset) {
    Polygon result;
    if (input.count == 0) {
        return result;
    }
    auto previous = input.vertices[input.count - 1];
    float previousDistance = glm::dot(normal, previous) + offset;
    for (std::size_t i = 0; i < input.count; ++i) {
        const auto point = input.vertices[i];
        const float distance = glm::dot(normal, point) + offset;
        // 落在边界上的端点直接由下面的内侧分支写入，避免同一个交点重复写入。
        if ((previousDistance < 0.0f && distance > 0.0f) ||
            (previousDistance > 0.0f && distance < 0.0f)) {
            result.vertices[result.count++] = glm::mix(previous, point, previousDistance / (previousDistance - distance));
        }
        if (distance >= 0.0f) {
            result.vertices[result.count++] = point;
        }
        previous = point;
        previousDistance = distance;
    }
    return result;
}

//————————————————————————————————————————————————————————————————————————————————
// 在地形当前三角面上取高，使用周围高度估算平滑法线。
//————————————————————————————————————————————————————————————————————————————————
SceneVertex RoadVertex(const HeightField& field, glm::vec2 p) {
    constexpr float d = HeightField::kSpacing * 0.5f;
    const float dx = field.HeightAt({p.x - d, p.y}) - field.HeightAt({p.x + d, p.y});
    const float dz = field.HeightAt({p.x, p.y - d}) - field.HeightAt({p.x, p.y + d});
    const auto normal = glm::normalize(glm::vec3{dx, 2.0f * d, dz});
    return {{p.x, field.HeightAt(p) + kRoadOffset, p.y}, {normal.x, normal.y, normal.z}, {0.39f, 0.255f, 0.13f}};
}

//————————————————————————————————————————————————————————————————————————————————
// 按相机颜色通道所需的朝上绕序输出多边形，忽略近零面积。
//————————————————————————————————————————————————————————————————————————————————
void AppendPolygon(std::vector<SceneVertex>& mesh, const HeightField& field, const Polygon& p) {
    for (std::size_t i = 1; i + 1 < p.count; ++i) {
        const auto a = p.vertices[0];
        const auto b = p.vertices[i];
        const auto c = p.vertices[i + 1];
        const float winding = (b.y - a.y) * (c.x - a.x) - (b.x - a.x) * (c.y - a.y);
        if (std::abs(winding) > 1.0e-8f) {
            mesh.insert(mesh.end(), {RoadVertex(field, a), RoadVertex(field, winding > 0.0f ? b : c),
                                     RoadVertex(field, winding > 0.0f ? c : b)});
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 地表分段是两个平面。将带状三角形分别裁进四边和 B-C 对角线。
//————————————————————————————————————————————————————————————————————————————————
void ClipToTerrain(std::vector<SceneVertex>& mesh, const HeightField& field,
                   glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    const Polygon road{{a, b, c}, 3};
    const auto first = glm::ivec2(glm::floor(glm::min(a, glm::min(b, c)) / HeightField::kSpacing));
    const auto last = glm::ivec2(glm::floor(glm::max(a, glm::max(b, c)) / HeightField::kSpacing));
    for (int z = std::max(first.y, HeightField::kFirstSample); z <= std::min(last.y, HeightField::kLastSample - 1); ++z) {
        for (int x = std::max(first.x, HeightField::kFirstSample); x <= std::min(last.x, HeightField::kLastSample - 1); ++x) {
            const float left = x * HeightField::kSpacing;
            const float bottom = z * HeightField::kSpacing;
            const float diagonal = left + bottom + HeightField::kSpacing;
            auto cell = Clip(road, {1, 0}, -left);
            cell = Clip(cell, {-1, 0}, left + HeightField::kSpacing);
            cell = Clip(cell, {0, 1}, -bottom);
            cell = Clip(cell, {0, -1}, bottom + HeightField::kSpacing);
            AppendPolygon(mesh, field, Clip(cell, {-1, -1}, diagonal));
            AppendPolygon(mesh, field, Clip(cell, {1, 1}, -diagonal));
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 将像素投到线段后把参数限制在两端内，返回世界距离的平方。
//————————————————————————————————————————————————————————————————————————————————
float DistanceSquared(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const auto ab = b - a;
    const float t = std::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
    const auto offset = p - (a + ab * t);
    return glm::dot(offset, offset);
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 首尾重复使用端点，采样步数随段长变化。
//————————————————————————————————————————————————————————————————————————————————
std::vector<glm::vec2> RoadSpline::Centerline() const {
    if (points.size() < kMinPoints) {
        return {};
    }
    std::vector<glm::vec2> path{points.front()};
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const auto p0 = points[i == 0 ? 0 : i - 1];
        const auto p1 = points[i];
        const auto p2 = points[i + 1];
        const auto p3 = points[std::min(i + 2, points.size() - 1)];
        const int steps = std::max(4, static_cast<int>(std::ceil(glm::length(p2 - p1) / 0.22f)));
        for (int j = 1; j <= steps; ++j) {
            const auto p = CatmullRom(p0, p1, p2, p3, static_cast<float>(j) / steps);
            const auto delta = p - path.back();
            if (glm::dot(delta, delta) > 1.0e-7f) {
                path.push_back(p);
            }
        }
    }
    return path;
}

//————————————————————————————————————————————————————————————————————————————————
// 用相邻样点的切线定左右方向，每段形成两个带状三角面。
//————————————————————————————————————————————————————————————————————————————————
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 主路与支路使用同一套断面和裁剪规则。
//————————————————————————————————————————————————————————————————————————————————
void AppendStrip(std::vector<SceneVertex>& mesh, const HeightField& field,
                 const std::vector<glm::vec2>& path, float width) {
    glm::vec2 left{}, right{};
    bool haveEdge = false;
    for (std::size_t i = 0; i < path.size() && path.size() >= 2; ++i) {
        const auto next = path[std::min(i + 1, path.size() - 1)];
        const auto previous = path[i == 0 ? 0 : i - 1];
        const auto axis = glm::length(next - previous) > 0.0001f ? next - previous
            : (i == 0 ? next - path[i] : path[i] - previous);
        if (glm::dot(axis, axis) < 1.0e-8f) continue;
        const auto tangent = glm::normalize(axis);
        const auto normal = glm::vec2{-tangent.y, tangent.x} * (width * 0.5f);
        const auto nextLeft = path[i] + normal;
        const auto nextRight = path[i] - normal;
        if (haveEdge) {
            ClipToTerrain(mesh, field, left, right, nextLeft);
            ClipToTerrain(mesh, field, right, nextRight, nextLeft);
        }
        left = nextLeft;
        right = nextRight;
        haveEdge = true;
    }
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 支路沿主路局部切线的垂线延伸。T 字只取一边，十字取两边。
//————————————————————————————————————————————————————————————————————————————————
std::vector<glm::vec2> RoadSpline::Branch() const {
    if (junction == RoadJunction::None || points.size() < 3) {
        return {};
    }
    const auto at = std::clamp<std::size_t>(junctionPoint, 1, points.size() - 2);
    const auto center = points[at];
    const auto axis = points[at + 1] - points[at - 1];
    const auto normal = glm::dot(axis, axis) > 1.0e-6f ? glm::normalize(glm::vec2{-axis.y, axis.x})
                                                     : glm::vec2{1, 0};
    const auto endpoint = [&](float sign) {
        return glm::clamp(center + sign * branchLength * normal, -kPointLimit, kPointLimit);
    };
    if (junction == RoadJunction::Cross) {
        return {endpoint(-1.0f), center, endpoint(1.0f)};
    }
    return {center, endpoint(1.0f)};
}

//————————————————————————————————————————————————————————————————————————————————
// 路口中心加过渡面，三个部分都被裁到同一个地形平面。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeRoadMesh(const HeightField& field, const RoadSpline& road) {
    std::vector<SceneVertex> mesh;
    AppendStrip(mesh, field, road.Centerline(), road.width);
    const auto branch = road.Branch();
    if (!branch.empty()) {
        AppendStrip(mesh, field, branch, road.width);
        const auto center = branch[road.junction == RoadJunction::Cross ? 1 : 0];
        constexpr int sides = 16;
        for (int i = 0; i < sides; ++i) {
            const float a = 6.283185307f * i / sides;
            const float b = 6.283185307f * (i + 1) / sides;
            ClipToTerrain(mesh, field, center,
                          center + (road.width * 0.5f) * glm::vec2{std::cos(a), std::sin(a)},
                          center + (road.width * 0.5f) * glm::vec2{std::cos(b), std::sin(b)});
        }
    }
    return mesh;
}

//————————————————————————————————————————————————————————————————————————————————
// 道路和输出遮罩使用同一条中心线，留边也包含道路开头与结尾。
//————————————————————————————————————————————————————————————————————————————————
RoadMask MakeRoadMask(const RoadSpline& road) {
    RoadMask mask;
    constexpr float spacing = 2.0f * HeightField::kHalfExtent / RoadMask::kSide;
    const float radius = road.width * 0.5f + road.margin;
    for (const auto& path : {road.Centerline(), road.Branch()}) for (std::size_t i = 1; i < path.size(); ++i) {
        const auto delta = path[i] - path[i - 1];
        if (glm::dot(delta, delta) < 1.0e-8f) continue;
        const auto low = (glm::min(path[i - 1], path[i]) - radius + HeightField::kHalfExtent) / spacing;
        const auto high = (glm::max(path[i - 1], path[i]) + radius + HeightField::kHalfExtent) / spacing;
        for (int z = std::max(0, static_cast<int>(std::floor(low.y)));
             z <= std::min(RoadMask::kSide - 1, static_cast<int>(std::floor(high.y))); ++z) {
            for (int x = std::max(0, static_cast<int>(std::floor(low.x)));
                 x <= std::min(RoadMask::kSide - 1, static_cast<int>(std::floor(high.x))); ++x) {
                const auto p = (glm::vec2{x, z} + 0.5f) * spacing - HeightField::kHalfExtent;
                if (DistanceSquared(p, path[i - 1], path[i]) <= radius * radius) {
                    mask.pixels[z * RoadMask::kSide + x] = 255;
                }
            }
        }
    }
    return mask;
}

}  // namespace tinyworld
