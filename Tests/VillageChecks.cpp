#include "Engine/Scene/GltfAsset.h"
#include "Engine/Scene/SceneFile.h"
#include "Engine/Scene/WorldScene.h"

#include <algorithm>
#include <chrono>
#include <glm/geometric.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

using namespace tinyworld;

//————————————————————————————————————————————————————————————————————————————————
// 验收失败即结束进程。Release 构建也照常运行这些检查。
//————————————————————————————————————————————————————————————————————————————————
void Require(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

//————————————————————————————————————————————————————————————————————————————————
// 检查每个课程原型都有网格、底色和金属粗糙度贴图，桥面尺寸与碰撞约定一致。
//————————————————————————————————————————————————————————————————————————————————
void CheckModels(const std::filesystem::path& root) {
    for (std::size_t i = kBuiltinPropCount; i < kPorchAsset; ++i) {
        const auto prototype = LoadGltfAsset(root / kPropAssets[i].id);
        Require(!prototype.primitives.empty(), "glTF must contain geometry.");
        for (const auto& primitive : prototype.primitives) {
            const auto& material = prototype.materials.at(primitive.material);
            Require(material.baseColor.width > 1 && material.metalRough.width > 1, "Missing glTF textures.");
        }
        if (i == kBridgeAsset) {
            float minZ = 0.0f, maxZ = 0.0f, minX = 0.0f, maxX = 0.0f;
            for (const auto& primitive : prototype.primitives) {
                for (const auto& vertex : primitive.vertices) {
                    const auto& p = vertex.geometry.position;
                    if (std::abs(p[1]) > 0.0001f || vertex.geometry.normal[1] < 0.99f) continue;
                    minX = std::min(minX, p[0]);
                    maxX = std::max(maxX, p[0]);
                    minZ = std::min(minZ, p[2]);
                    maxZ = std::max(maxZ, p[2]);
                }
            }
            Require(std::abs(minX + kBridgeDeckHalfWidth) < 0.0001f &&
                    std::abs(maxX - kBridgeDeckHalfWidth) < 0.0001f &&
                    std::abs(minZ + kBridgeDeckHalfLength) < 0.0001f &&
                    std::abs(maxZ - kBridgeDeckHalfLength) < 0.0001f, "Bridge geometry and collision disagree.");
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 从引道直走 6 秒。分别在桥中央和远端引道检查脚底所在表面。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 CrossBridge(WorldScene& scene, int fps) {
    scene.SetWalking(true);
    for (int frame = 1; frame <= 6 * fps; ++frame) {
        const float previousY = scene.WalkerPose().position.y;
        scene.UpdateWalk({1.0f, 0.0f}, 1.0 / fps);
        const auto feet = scene.WalkerPose().position;
        Require(std::abs(feet.y - previousY) < 0.14f, "Unexpected height jump while crossing the bridge.");
        if (frame == 3 * fps) {
            const float deckY = scene.props.front().transform.position.y;
            Require(std::abs(feet.z) < 0.30f && std::abs(feet.y - deckY) < 0.001f, "Walker missed the deck.");
            Require(feet.y - scene.terrain->HeightAt({feet.x, feet.z}) > 1.0f, "Walker stuck to the stream bed.");
        }
    }
    const auto end = scene.WalkerPose().position;
    scene.SetWalking(false);
    Require(end.z < -5.0f && std::abs(end.y - scene.terrain->HeightAt({end.x, end.z})) < 0.001f,
            "Walker did not leave the bridge onto the far road.");
    return end;
}

//————————————————————————————————————————————————————————————————————————————————
// 在旧场景、读写往返及坏文件之间切换，确认每次加载都提交完整状态。
//————————————————————————————————————————————————————————————————————————————————
void CheckScenes(const std::filesystem::path& root, const std::filesystem::path& temporary) {
    WorldScene scene;
    scene.terrain.emplace();
    scene.canSculpt = true;
    scene.road.emplace();
    scene.canPlaceProps = true;
    scene.canUseGltfProps = true;
    scene.vegetation.emplace().mode = VegetationMode::TerrainRules;

    LoadScene(root / "scenes/ch07/valley.json", scene);
    Require(scene.canUseGltfProps && scene.props.empty(), "Chapter 07 file did not reopen in Chapter 13.");
    LoadScene(root / "scenes/ch13/village.json", scene);
    Require(scene.props.size() == 9 && scene.props.front().asset == kBridgeAsset, "Could not reopen village references.");
    scene.props[1].transform.position.x -= 0.2f;
    const auto expected = scene.props;
    SaveScene(temporary, scene);

    LoadScene(temporary, scene);
    Require(scene.props.size() == expected.size(), "Lost instances on scene round trip.");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& actual = scene.props[i];
        const auto& saved = expected[i];
        Require(actual.asset == saved.asset && actual.transform.Matrix() == saved.transform.Matrix(),
                "Lost asset or transform on scene round trip.");
    }

    {
        std::ofstream invalid(temporary, std::ios::trunc);
        invalid << "{\"version\":999}";
    }
    bool rejected = false;
    try {
        LoadScene(temporary, scene);
    } catch (const std::exception&) {
        rejected = true;
    }
    Require(rejected && scene.props.size() == expected.size() &&
            scene.props[1].transform.position == expected[1].transform.position,
            "A failed scene load affected the open scene.");

    const auto at24 = CrossBridge(scene, 24);
    const auto at60 = CrossBridge(scene, 60);
    Require(glm::length(at24 - at60) < 0.001f, "Walk results changed with frame rate.");

    // 从沟底沿 X 走过桥侧，过高的桥面不可直接迈上。
    scene.spawn = WalkSpawn{{-1.65f, 0.0f}, 90.0f};
    scene.SetWalking(true);
    for (int frame = 0; frame < 60; ++frame) scene.UpdateWalk({1.0f, 0.0f}, 1.0 / 60);
    const auto under = scene.WalkerPose().position;
    Require(std::abs(under.x) < 0.30f && under.y < -1.0f, "Walker snapped upwards from under the bridge.");
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 运行村庄的 CPU 验收，临时存档放在构建目录中并在退出前删除。
//————————————————————————————————————————————————————————————————————————————————
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = std::filesystem::path(argv[2]) / ("village-test-" + std::to_string(nonce) + ".json");
    int result = 0;
    try {
        CheckModels(argv[1]);
        CheckScenes(argv[1], temporary);
        std::cout << "Village asset, scene round trip, bridge walk and fixed-step checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return result;
}
