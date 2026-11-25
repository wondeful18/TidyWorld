#include "Engine/Renderer/RenderCoordinates.h"

#include <glm/ext/matrix_transform.hpp>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 保留相同的相机旋转，只改变平移所处的精度和坐标系。
//————————————————————————————————————————————————————————————————————————————————
RenderCoordinates::RenderCoordinates(const OrbitCamera& camera, const glm::mat4& projection,
                                       const PrecisionPreview& preview) {
    if (!preview.offsetWorld) {
        mViewProjection = projection * camera.View();
        return;
    }
    const glm::dvec3 worldOrigin{PrecisionPreview::kOriginOffset, 0.0, PrecisionPreview::kOriginOffset};
    const glm::dvec3 eye = worldOrigin + glm::dvec3(camera.Position());
    const glm::mat4 rotation{glm::mat3(camera.View())};
    if (preview.cameraRelative) {
        mModelOffset = worldOrigin - eye;
        mViewProjection = projection * rotation;
    } else {
        mModelOffset = worldOrigin;
        mViewProjection = projection * glm::translate(rotation, -glm::vec3(eye));
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 在 double 中合成平移，再只转换一次。模型的方向和缩放继续使用 float。
//————————————————————————————————————————————————————————————————————————————————
glm::mat4 RenderCoordinates::Model(const glm::mat4& mapModel) const {
    auto result = mapModel;
    const glm::dvec3 translation = glm::dvec3(mapModel[3]) + mModelOffset;
    result[3] = {glm::vec3(translation), 1.0f};
    return result;
}

}  // namespace tinyworld
