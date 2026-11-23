#include "Engine/Scene/WalkerMesh.h"

#include "Engine/Scene/PrimitiveMesh.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 用少量有底色的盒体构成可从远处辨认的角色。尺寸以米计。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeWalkerMesh() {
    using primitive::AddBox;
    using primitive::Color;
    primitive::Mesh mesh;
    constexpr Color hoodie{0.86f, 0.49f, 0.035f};
    constexpr Color pants{0.065f, 0.115f, 0.16f};
    constexpr Color shoes{0.033f, 0.039f, 0.045f};
    constexpr Color skin{0.82f, 0.49f, 0.27f};
    constexpr Color hair{0.11f, 0.055f, 0.026f};
    constexpr Color pack{0.18f, 0.57f, 0.37f};

    AddBox(mesh, {-0.17f, 0.54f, -0.12f}, {0.17f, 0.99f, 0.13f}, hoodie, {0.03f, 0.018f});
    AddBox(mesh, {-0.11f, 0.99f, -0.09f}, {0.11f, 1.24f, 0.12f}, skin, {0.013f, 0.01f});
    AddBox(mesh, {-0.115f, 1.17f, -0.105f}, {0.115f, 1.29f, 0.125f}, hair, {0.024f, 0.02f});
    AddBox(mesh, {-0.133f, 0.63f, -0.229f}, {0.133f, 0.93f, -0.105f}, pack, {0.024f, 0.017f});
    for (const float side : {-1.0f, 1.0f}) {
        const float arm = side * 0.211f;
        const float leg = side * 0.079f;
        AddBox(mesh, {arm - 0.047f, 0.58f, -0.055f}, {arm + 0.047f, 0.91f, 0.064f}, hoodie);
        AddBox(mesh, {arm - 0.042f, 0.514f, -0.049f}, {arm + 0.042f, 0.59f, 0.058f}, skin);
        AddBox(mesh, {leg - 0.068f, 0.072f, -0.075f}, {leg + 0.068f, 0.55f, 0.082f}, pants);
        AddBox(mesh, {leg - 0.073f, 0.0f, -0.085f}, {leg + 0.073f, 0.093f, 0.145f}, shoes);
    }
    return mesh;
}

}  // namespace tinyworld
