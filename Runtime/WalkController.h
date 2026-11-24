#pragma once

#include "Engine/Scene/OrbitCamera.h"
#include "Engine/Scene/Transform.h"

#include <glm/vec2.hpp>

namespace tinyworld {

class HeightField;

//————————————————————————————————————————————————————————————————————————————————
// 平地章使用草台局部空间，地形章使用世界 XZ 与绕世界 Y 的朝向。
//————————————————————————————————————————————————————————————————————————————————
struct WalkSpawn {
    static constexpr float kLimitX = 5.4f;  // 留出草台圆角与脚部所需的余量
    static constexpr float kLimitZ = 3.8f;  // 同上，单位为模型空间米
    glm::vec2 groundXZ{-0.45f, 3.25f};      // 局部 X、Z，脚底的局部 Y 固定为零
    float yaw{165.0f};                      // 从局部 +Z 绕 Y 轴旋转的角度，单位为度

    //————————————————————————————————————————————————————————————————————————————————
    // 将地面上的落脚点和朝向转换成世界空间的直立角色变换。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] Transform WorldPose(const Transform& ground, const HeightField* terrain = nullptr) const;
};

//————————————————————————————————————————————————————————————————————————————————
// 每帧采样的步行指令。前进与左转为正，倒走与右转为负。
//————————————————————————————————————————————————————————————————————————————————
struct WalkInput {
    float forward{};  // 沿角色面向移动的方向
    float turn{};     // 改变面向的方向
    bool reset{};     // 本帧回到场景出生点
};

struct WalkGait {
    float phase{};      // 左右脚相差半个周期，单位为弧度
    float strength{};   // 起步与停步时渐变
    float direction{1.0f};
};

//————————————————————————————————————————————————————————————————————————————————
// 在草台上以固定时间步推进脚底位置，再更新第三人称跟随镜头。
//————————————————————————————————————————————————————————————————————————————————
class WalkController {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 拷贝出生点成为运行时状态，并计算第一帧的角色与相机。
    //————————————————————————————————————————————————————————————————————————————————
    WalkController(const WalkSpawn& spawn, const Transform& ground, const HeightField* terrain = nullptr);

    //————————————————————————————————————————————————————————————————————————————————
    // 累积本帧秒数，执行完整的模拟步。reset 会清空剩余时间。
    //————————————————————————————————————————————————————————————————————————————————
    void Update(const WalkSpawn& spawn, const Transform& ground, WalkInput input, double deltaSeconds,
                const HeightField* terrain = nullptr);

    //————————————————————————————————————————————————————————————————————————————————
    // 返回可直接用于角色模型矩阵的脚底变换。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] const Transform& Pose() const { return mPose; }
    [[nodiscard]] WalkGait Gait() const { return mGait; }

    //————————————————————————————————————————————————————————————————————————————————
    // 返回角色后方的相机，供颜色通道和环境反射读取。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] const OrbitCamera& Camera() const { return mCamera; }

private:
    //————————————————————————————————————————————————————————————————————————————————
    // 把最后一个模拟步的结果转换为本帧的角色与镜头变换。
    //————————————————————————————————————————————————————————————————————————————————
    void Follow(const Transform& ground, const HeightField* terrain);

    WalkSpawn mFeet;        // 当前脚底 XZ 和朝向，与出生点采用同一坐标空间
    double mRemaining{};   // 尚未凑满一个时间步的秒数
    Transform mPose;        // 供渲染器使用的世界脚底变换
    WalkGait mGait;
    OrbitCamera mCamera;    // 独立于编辑和驾驶相机的第三人称相机
};

}  // namespace tinyworld
