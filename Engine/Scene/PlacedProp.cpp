#include "Engine/Scene/PlacedProp.h"

#include "Engine/Scene/PrimitiveMesh.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 以坡面落点为原点构造有少许埋入地面的不规则石块。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeRockMesh() {
    using primitive::AddFace;
    primitive::Mesh mesh;
    const glm::vec3 a{-0.72f, -0.10f, -0.42f}, b{0.63f, -0.10f, -0.47f};
    const glm::vec3 c{0.80f, -0.10f, 0.33f}, d{-0.56f, -0.10f, 0.53f};
    const glm::vec3 e{-0.38f, 0.44f, -0.25f}, f{0.28f, 0.69f, -0.19f};
    const glm::vec3 g{0.52f, 0.35f, 0.23f}, h{-0.20f, 0.58f, 0.39f};
    AddFace(mesh, std::array{a, b, c, d}, {0.24f, 0.23f, 0.21f});
    AddFace(mesh, std::array{e, h, f}, {0.40f, 0.41f, 0.38f});
    AddFace(mesh, std::array{h, g, f}, {0.34f, 0.35f, 0.33f});
    AddFace(mesh, std::array{a, e, b}, {0.25f, 0.27f, 0.26f});
    AddFace(mesh, std::array{e, f, b}, {0.32f, 0.32f, 0.30f});
    AddFace(mesh, std::array{b, f, g}, {0.28f, 0.30f, 0.29f});
    AddFace(mesh, std::array{b, g, c}, {0.24f, 0.25f, 0.24f});
    AddFace(mesh, std::array{c, g, h}, {0.36f, 0.35f, 0.31f});
    AddFace(mesh, std::array{c, h, d}, {0.29f, 0.29f, 0.26f});
    AddFace(mesh, std::array{d, h, e}, {0.30f, 0.32f, 0.29f});
    AddFace(mesh, std::array{d, e, a}, {0.23f, 0.25f, 0.23f});
    return mesh;
}

//————————————————————————————————————————————————————————————————————————————————
// 用盒体搭地基、墙和门窗，再用三角面与四边形盖双坡屋顶。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> MakeCabinMesh() {
    using primitive::AddBox;
    using primitive::AddFace;
    primitive::Mesh mesh;
    AddBox(mesh, {-1.12f, -0.36f, -0.87f}, {1.12f, 0.12f, 0.87f}, {0.28f, 0.28f, 0.23f});
    AddBox(mesh, {-1.06f, 0.12f, -0.82f}, {1.06f, 1.55f, 0.82f}, {0.45f, 0.25f, 0.13f});
    AddBox(mesh, {-0.27f, 0.12f, 0.81f}, {0.21f, 1.25f, 0.87f}, {0.15f, 0.09f, 0.055f});
    AddBox(mesh, {0.43f, 0.69f, 0.81f}, {0.85f, 1.13f, 0.86f}, {0.20f, 0.45f, 0.56f});
    AddBox(mesh, {0.61f, 0.68f, 0.86f}, {0.66f, 1.15f, 0.88f}, {0.68f, 0.46f, 0.24f});
    const glm::vec3 a{-1.23f, 1.52f, -1.0f}, b{1.23f, 1.52f, -1.0f};
    const glm::vec3 c{1.23f, 1.52f, 1.0f}, d{-1.23f, 1.52f, 1.0f};
    const glm::vec3 back{0.0f, 2.30f, -1.0f}, front{0.0f, 2.30f, 1.0f};
    AddFace(mesh, std::array{a, b, c, d}, {0.10f, 0.14f, 0.15f});
    AddFace(mesh, std::array{a, d, front, back}, {0.13f, 0.26f, 0.26f});
    AddFace(mesh, std::array{back, front, c, b}, {0.10f, 0.22f, 0.23f});
    AddFace(mesh, std::array{a, back, b}, {0.31f, 0.17f, 0.09f});
    AddFace(mesh, std::array{d, c, front}, {0.35f, 0.20f, 0.10f});
    return mesh;
}

}  // namespace tinyworld
