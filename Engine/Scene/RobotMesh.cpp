#include "Engine/Scene/RobotMesh.h"

#include "Engine/Scene/PrimitiveMesh.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 用头壳、面板、短腿和天线组成容易辨认的机器人轮廓。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeRobotMesh() {
    using primitive::AddBox;
    using primitive::Color;
    primitive::Mesh mesh;
    constexpr Color shell{0.71f, 0.77f, 0.69f};
    constexpr Color teal{0.035f, 0.39f, 0.39f};
    constexpr Color dark{0.026f, 0.069f, 0.084f};
    constexpr Color yellow{0.96f, 0.44f, 0.06f};
    constexpr Color led{0.40f, 0.91f, 0.86f};

    AddBox(mesh, {-0.215f, 0.78f, -0.16f}, {0.215f, 1.16f, 0.16f}, shell, {0.032f, 0.025f});
    AddBox(mesh, {-0.174f, 0.848f, 0.154f}, {0.174f, 1.085f, 0.181f}, dark);
    AddBox(mesh, {-0.17f, 0.27f, -0.142f}, {0.17f, 0.73f, 0.142f}, teal, {0.025f, 0.021f});
    AddBox(mesh, {-0.115f, 0.39f, -0.18f}, {0.115f, 0.64f, -0.122f}, dark, {0.02f, 0.012f});
    AddBox(mesh, {-0.072f, 0.49f, -0.192f}, {0.072f, 0.526f, -0.175f}, yellow);
    AddBox(mesh, {-0.065f, 0.69f, -0.058f}, {0.065f, 0.81f, 0.058f}, dark);
    AddBox(mesh, {0.075f, 1.12f, -0.022f}, {0.105f, 1.34f, 0.018f}, dark);
    AddBox(mesh, {0.048f, 1.30f, -0.045f}, {0.133f, 1.39f, 0.043f}, yellow, {0.012f, 0.01f});

    for (const float side : {-1.0f, 1.0f}) {
        const float eye = side * 0.077f;
        const float arm = side * 0.205f;
        const float leg = side * 0.091f;
        AddBox(mesh, {eye - 0.028f, 0.944f, 0.179f}, {eye + 0.028f, 1.011f, 0.19f}, led);
        AddBox(mesh, {arm - 0.042f, 0.38f, -0.068f}, {arm + 0.042f, 0.67f, 0.068f}, yellow, {0.012f, 0.01f});
        AddBox(mesh, {leg - 0.052f, 0.072f, -0.062f}, {leg + 0.052f, 0.31f, 0.066f}, dark);
        AddBox(mesh, {leg - 0.068f, 0.0f, -0.101f}, {leg + 0.068f, 0.11f, 0.136f}, shell);
    }
    return mesh;
}

}  // namespace tinyworld
