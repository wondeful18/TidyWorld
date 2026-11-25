#pragma once

#include "Engine/Scene/WorldScene.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 地图坐标仍用于编辑、碰撞和光照。相机投影单独选择绝对或相对坐标。
//————————————————————————————————————————————————————————————————————————————————
class RenderCoordinates {
public:
    RenderCoordinates(const OrbitCamera& camera, const glm::mat4& projection, const PrecisionPreview& preview);
    [[nodiscard]] glm::mat4 Model(const glm::mat4& mapModel) const;
    [[nodiscard]] const glm::mat4& ViewProjection() const { return mViewProjection; }

private:
    glm::dvec3 mModelOffset{0.0}; // 绝对模式加世界原点；相对模式在 double 中先减相机
    glm::mat4 mViewProjection{1.0f}; // 光栅化使用的视图投影
};

}  // namespace tinyworld
