#include "Engine/Scene/Transform.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 列向量先缩放、再旋转、最后平移。界面输入的角度在这里转换为弧度。
//————————————————————————————————————————————————————————————————————————————————
glm::mat4 Transform::Matrix() const {
    const glm::mat4 translate = glm::translate(glm::mat4{1.0f}, position);
    const glm::mat4 rotate = glm::mat4_cast(glm::quat(glm::radians(rotationDegrees)));
    return translate * rotate * glm::scale(glm::mat4{1.0f}, scale);
}

}  // namespace tinyworld
