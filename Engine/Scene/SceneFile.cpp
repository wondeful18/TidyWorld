#include "Engine/Scene/SceneFile.h"

#include "Engine/Scene/DayCycle.h"

#include "Engine/Scene/WorldScene.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace tinyworld {
namespace {

using Json = nlohmann::json;
constexpr int kSceneVersion = 1;
constexpr int kTerrainVersion = 2;
constexpr int kRoadVersion = 3;
constexpr int kPropVersion = 4;
constexpr int kEcologyVersion = 5;
constexpr int kVillageVersion = 6;
constexpr int kLightmapVersion = 7;
constexpr int kDayCycleVersion = 8;
constexpr int kCapstoneVersion = 9;
constexpr int kLegacyHeightSide = 65;
constexpr int kLegacyPaintSide = 128;
constexpr float kCoordinateLimit = 1000000.0f;

//————————————————————————————————————————————————————————————————————————————————
// 按 X、Y、Z 的顺序保存向量，保持 JSON 可读。
//————————————————————————————————————————————————————————————————————————————————
Json WriteVec3(const glm::vec3& value) {
    return Json::array({value.x, value.y, value.z});
}

//————————————————————————————————————————————————————————————————————————————————
// 类型、有限性和范围都通过校验后才转换为场景所用的 float。
//————————————————————————————————————————————————————————————————————————————————
float ReadNumber(const Json& value, float low = -kCoordinateLimit, float high = kCoordinateLimit) {
    if (!value.is_number()) {
        throw std::runtime_error("Expected a scene number.");
    }
    const auto number = value.get<double>();
    if (!std::isfinite(number) || number < low || number > high) {
        throw std::runtime_error("Scene number is out of range.");
    }
    return static_cast<float>(number);
}

//————————————————————————————————————————————————————————————————————————————————
// 要求数组长度完全匹配，再逐轴读取数值。
//————————————————————————————————————————————————————————————————————————————————
void RequireArray(const Json& value, std::size_t size) {
    if (!value.is_array() || value.size() != size) {
        throw std::runtime_error("Invalid scene array length.");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 读取三个轴，可复用同一范围约束来检查缩放和颜色。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 ReadVec3(const Json& value, float low = -kCoordinateLimit, float high = kCoordinateLimit) {
    RequireArray(value, 3);
    return {ReadNumber(value[0], low, high), ReadNumber(value[1], low, high), ReadNumber(value[2], low, high)};
}

//————————————————————————————————————————————————————————————————————————————————
// 常驻物体和手摆实例共用三组变换的校验规则。
//————————————————————————————————————————————————————————————————————————————————
Transform ReadTransform(const Json& entry) {
    return {ReadVec3(entry.at("position")), ReadVec3(entry.at("rotation_deg")),
            ReadVec3(entry.at("scale"), 0.0001f, 10000.0f)};
}

//————————————————————————————————————————————————————————————————————————————————
// 将旧样区放回新高度表的同一组世界坐标，向外过渡到默认地形。
//————————————————————————————————————————————————————————————————————————————————
void ReadHeightField(const Json& data, HeightField& field) {
    const auto& side = data.at("samples_per_axis");
    const auto grid = field.Grid();
    const bool legacy = grid == TerrainGrid{} && side == kLegacyHeightSide;
    if (data.at("spacing_m") != HeightField::kSpacing || (side != grid.SampleSide() && !legacy)) {
        throw std::runtime_error("Unsupported heightfield dimensions.");
    }
    const int savedSide = side.get<int>();
    const auto& entries = data.at("heights_m");
    RequireArray(entries, static_cast<std::size_t>(savedSide * savedSide));
    auto samples = field.Values();
    if (!legacy) {
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = ReadNumber(entries[i], -HeightField::kHeightLimit, HeightField::kHeightLimit);
        }
    } else {
        constexpr int apron = (HeightField::kSampleCount - kLegacyHeightSide) / 2;
        std::array<float, kLegacyHeightSide * kLegacyHeightSide> saved;
        for (std::size_t i = 0; i < saved.size(); ++i) {
            saved[i] = ReadNumber(entries[i], -HeightField::kHeightLimit, HeightField::kHeightLimit);
        }
        const auto& base = field.Values();
        for (int z = 0; z < HeightField::kSampleCount; ++z) {
            for (int x = 0; x < HeightField::kSampleCount; ++x) {
                const int oldX = std::clamp(x - apron, 0, kLegacyHeightSide - 1);
                const int oldZ = std::clamp(z - apron, 0, kLegacyHeightSide - 1);
                const auto edge = (oldZ + apron) * HeightField::kSampleCount + oldX + apron;
                const auto index = z * HeightField::kSampleCount + x;
                const float previous = saved[oldZ * kLegacyHeightSide + oldX];
                if (index == edge) {
                    samples[index] = previous;
                } else {
                    // 旧边界处的编辑逐渐过渡到外圈生成地形。
                    const int distance = std::max(std::abs(x - oldX - apron), std::abs(z - oldZ - apron));
                    const float weight = 1.0f - static_cast<float>(distance) / apron;
                    samples[index] = std::clamp(base[index] + weight * (previous - base[edge]),
                                                -HeightField::kHeightLimit, HeightField::kHeightLimit);
                }
            }
        }
    }
    field.ReplaceSamples(samples);
}

//————————————————————————————————————————————————————————————————————————————————
// 只编码影响下次打开的编辑状态，游览控制器只是临时状态。
//————————————————————————————————————————————————————————————————————————————————
Json Encode(const WorldScene& scene) {
    Json objects = Json::array();
    for (const auto& object : scene.objects) {
        const auto& t = object.transform;
        objects.push_back({{"id", object.id}, {"asset", object.asset},
                           {"position", WriteVec3(t.position)}, {"rotation_deg", WriteVec3(t.rotationDegrees)},
                           {"scale", WriteVec3(t.scale)}});
    }
    const auto& spawn = scene.spawn.value();
    const auto& shadow = scene.shadows.value();
    const auto& sky = scene.environment.value();
    const bool ecology = scene.vegetation && scene.vegetation->mode == VegetationMode::TerrainRules;
    const bool large = scene.terrain && scene.terrain->Grid() == TerrainGrid(true);
    const int version = large ? kCapstoneVersion : scene.dayCycle ? kDayCycleVersion
        : scene.lightmapEnabled ? kLightmapVersion : scene.canUseGltfProps ? kVillageVersion
        : ecology ? kEcologyVersion : scene.canPlaceProps ? kPropVersion
        : scene.road ? kRoadVersion : scene.terrain ? kTerrainVersion : kSceneVersion;
    Json doc = {
        {"version", version},
        {"objects", objects},
        {"spawn", {{scene.terrain ? "world_xz" : "ground_xz", Json::array({spawn.groundXZ.x, spawn.groundXZ.y})},
                   {"yaw_deg", spawn.yaw}}},
        {"camera", {{"target", WriteVec3(scene.camera.target)}, {"azimuth_deg", scene.camera.azimuth},
                    {"elevation_deg", scene.camera.elevation}, {"distance_m", scene.camera.distance}}},
        {"sun", {{"azimuth_deg", scene.sun.azimuth}, {"elevation_deg", scene.sun.elevation},
                 {"intensity", scene.sun.intensity}, {"color", WriteVec3(scene.sun.color)}}},
        {"shadows", {{"enabled", shadow.enabled}, {"pcf", shadow.pcf}, {"depth_bias", shadow.depthBias}}},
        {"environment", {{"sky", sky.sky == SkyMode::Cubemap ? "cubemap" : "panorama"},
                         {"car_reflection", sky.carReflection}}},
    };
    if (scene.dayCycle) doc["time_hours"] = WrapDayHour(scene.dayCycle->hour);
    if (scene.terrain) {
        doc["heightfield"] = {{"spacing_m", HeightField::kSpacing},
                              {"samples_per_axis", scene.terrain->Grid().SampleSide()},
                              {"heights_m", scene.terrain->Values()}};
    }
    if (scene.road) {
        Json points = Json::array();
        Json offsets = Json::array();
        bool hasOffsets = false;
        for (const auto& point : scene.road->points) {
            points.push_back({point.x, point.z});
            offsets.push_back(point.y);
            hasOffsets |= point.y != 0.0f;
        }
        doc["road"] = {{"control_xz", std::move(points)}, {"width_m", scene.road->width}, {"margin_m", scene.road->margin}};
        if (hasOffsets) {
            doc["road"]["control_y_offset_m"] = std::move(offsets);
        }
        doc["road"]["surface"] = static_cast<int>(scene.road->surface);
        doc["road"]["junction"] = static_cast<int>(scene.road->junction);
        doc["road"]["junction_point"] = scene.road->junctionPoint;
        doc["road"]["branch_length_m"] = scene.road->branchLength;
    }
    if (scene.paint && scene.paint->Revision() != 0) {
        constexpr std::string_view hex = "0123456789abcdef";
        std::string bytes;
        bytes.reserve(MaterialSplat::kSide * MaterialSplat::kSide * 8);
        for (const auto& pixel : scene.paint->Values()) {
            for (const auto channel : pixel) {
                bytes.push_back(hex[channel >> 4]);
                bytes.push_back(hex[channel & 15]);
            }
        }
        doc["terrain_weights"] = {{"size", MaterialSplat::kSide}, {"rgba_hex", std::move(bytes)}};
    }
    if (scene.canPlaceProps) {
        auto& props = doc["props"] = Json::array();
        for (const auto& prop : scene.props) {
            const auto& t = prop.transform;
            props.push_back({{"asset", kPropAssets.at(prop.asset).id}, {"position", WriteVec3(t.position)},
                             {"rotation_deg", WriteVec3(t.rotationDegrees)}, {"scale", WriteVec3(t.scale)}});
            if (!prop.lightmap.empty()) props.back()["lightmap"] = prop.lightmap;
        }
    }
    if (ecology) {
        auto& vegetation = doc["vegetation"] = {{"seed", scene.vegetation->seed}, {"density", scene.vegetation->density}};
        if (scene.canUseGltfProps && scene.vegetation->minPlantHeight) {
            vegetation["min_plant_y_m"] = *scene.vegetation->minPlantHeight;
        }
    }
    return doc;
}

//————————————————————————————————————————————————————————————————————————————————
// 按稳定 ID 合并固定布局，允许调整 JSON 记录顺序。相对资产必须匹配本章已部署的模型。
//————————————————————————————————————————————————————————————————————————————————
void ReadObjects(const Json& entries, WorldScene& scene) {
    RequireArray(entries, WorldScene::kObjectCount);
    std::array<bool, WorldScene::kObjectCount> seen{};
    for (const auto& entry : entries) {
        const auto id = entry.at("id").get<std::string>();
        const auto match = std::find_if(scene.objects.begin(), scene.objects.end(),
                                        [&](const WorldObject& object) { return id == object.id; });
        if (match == scene.objects.end()) {
            throw std::runtime_error("Unknown scene object ID.");
        }
        const auto index = static_cast<std::size_t>(match - scene.objects.begin());
        if (seen[index] || entry.at("asset") != match->asset) {
            throw std::runtime_error("Duplicate ID or mismatched scene asset.");
        }
        seen[index] = true;
        match->transform = ReadTransform(entry);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 在局部场景中读取文件内容，成功后交由调用者提交。
//————————————————————————————————————————————————————————————————————————————————
WorldScene Decode(const Json& doc) {
    const auto& version = doc.at("version");
    if (!version.is_number_integer() || version < kSceneVersion || version > kCapstoneVersion) {
        throw std::runtime_error("Unsupported scene file version.");
    }
    WorldScene scene;
    const bool terrain = version != kSceneVersion;
    if (version.get<int>() >= kLightmapVersion) scene.lightmapEnabled = true;
    if (version.get<int>() >= kDayCycleVersion) {
        scene.dayCycle.emplace().hour = WrapDayHour(ReadNumber(doc.at("time_hours"), 0.0f, 24.0f));
    }
    if (terrain) {
        ReadHeightField(doc.at("heightfield"), scene.terrain.emplace(TerrainGrid{version == kCapstoneVersion}));
        scene.canSculpt = true;
    }
    if (version.get<int>() >= kRoadVersion) {
        const auto& data = doc.at("road");
        const auto& points = data.at("control_xz");
        if (!points.is_array() || points.size() < RoadSpline::kMinPoints || points.size() > RoadSpline::kMaxPoints) {
            throw std::runtime_error("Invalid number of road control points.");
        }
        auto& road = scene.road.emplace();
        road.points.clear();
        const auto offsets = data.find("control_y_offset_m");
        if (offsets != data.end()) RequireArray(*offsets, points.size());
        const float pointLimit = scene.terrain->Grid().HalfExtent() - 0.5f;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& item = points[i];
            RequireArray(item, 2);
            const float y = offsets == data.end() ? 0.0f
                : ReadNumber((*offsets)[i], -RoadSpline::kHeightOffsetLimit, RoadSpline::kHeightOffsetLimit);
            road.points.emplace_back(ReadNumber(item[0], -pointLimit, pointLimit),
                                     y,
                                     ReadNumber(item[1], -pointLimit, pointLimit));
        }
        road.width = ReadNumber(data.at("width_m"), RoadSpline::kMinimumWidth, RoadSpline::kMaximumWidth);
        road.margin = ReadNumber(data.at("margin_m"), 0.0f, RoadSpline::kMaximumMargin);
        const auto readOption = [&](const char* key, int maxValue, int fallback) {
            if (!data.contains(key)) return fallback;
            const auto& value = data.at(key);
            if (!value.is_number_integer()) throw std::runtime_error("Expected an integer road option.");
            return static_cast<int>(ReadNumber(value, 0.0f, static_cast<float>(maxValue)));
        };
        road.surface = static_cast<RoadSurface>(readOption("surface", 2, 0));
        road.junction = static_cast<RoadJunction>(readOption("junction", 2, 0));
        road.junctionPoint = static_cast<std::size_t>(readOption("junction_point", static_cast<int>(RoadSpline::kMaxPoints - 1), 3));
        road.branchLength = data.contains("branch_length_m") ? ReadNumber(data.at("branch_length_m"), 2.0f, 8.0f) : 5.0f;
    }
    if (terrain && doc.contains("terrain_weights")) {
        const auto& paint = doc.at("terrain_weights");
        const auto bytes = paint.at("rgba_hex").get<std::string>();
        const auto& size = paint.at("size");
        if (size != MaterialSplat::kSide && size != kLegacyPaintSide) {
            throw std::runtime_error("Invalid terrain paint dimensions.");
        }
        const int side = size.get<int>();
        if (bytes.size() != static_cast<std::size_t>(side * side * 8)) {
            throw std::runtime_error("Invalid terrain paint data.");
        }
        constexpr std::string_view hex = "0123456789abcdef";
        MaterialSplat::Pixels pixels(MaterialSplat::kSide * MaterialSplat::kSide);
        const int apron = (MaterialSplat::kSide - side) / 2;
        std::size_t index = 0;
        for (int z = apron; z < apron + side; ++z) {
            for (int x = apron; x < apron + side; ++x) {
                for (auto& channel : pixels[z * MaterialSplat::kSide + x]) {
                    const auto high = hex.find(bytes[index++]);
                    const auto low = hex.find(bytes[index++]);
                    if (high == std::string_view::npos || low == std::string_view::npos) {
                        throw std::runtime_error("Invalid terrain paint channel.");
                    }
                    channel = static_cast<std::uint8_t>(high * 16 + low);
                }
            }
        }
        scene.paint.emplace().Replace(pixels);
    }
    ReadObjects(doc.at("objects"), scene);
    if (version.get<int>() >= kPropVersion) {
        scene.canPlaceProps = true;
        scene.canUseGltfProps = version.get<int>() >= kVillageVersion;
        const auto& props = doc.at("props");
        if (!props.is_array() || props.size() > PlacedProp::kLimit) {
            throw std::runtime_error("Invalid number of placed props.");
        }
        for (const auto& entry : props) {
            const auto id = entry.at("asset").get<std::string>();
            const auto asset = std::find_if(kPropAssets.begin(), kPropAssets.end(),
                                            [&](const PropAsset& item) { return id == item.id; });
            if (asset == kPropAssets.end()) throw std::runtime_error("Unknown placed prop asset.");
            const auto index = static_cast<std::size_t>(asset - kPropAssets.begin());
            if (index >= kBuiltinPropCount && !scene.canUseGltfProps) {
                throw std::runtime_error("glTF props require a village scene.");
            }
            std::string lightmap;
            if (entry.contains("lightmap")) {
                lightmap = entry.at("lightmap").get<std::string>();
                if (!scene.lightmapEnabled || !asset->lightmap || lightmap != asset->lightmap) {
                    throw std::runtime_error("Lightmap path does not match this prop asset.");
                }
            }
            if (asset->lightmap && !scene.lightmapEnabled) {
                throw std::runtime_error("The baked porch requires a Chapter 17 scene.");
            }
            scene.props.push_back({index, ReadTransform(entry), std::move(lightmap)});
        }
    }
    if (version == kEcologyVersion || (version.get<int>() >= kVillageVersion && doc.contains("vegetation"))) {
        const auto& settings = doc.at("vegetation");
        const auto& seed = settings.at("seed");
        if (!seed.is_number_integer() || seed < 0 || seed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Vegetation seed must be a 32-bit unsigned integer.");
        }
        auto& vegetation = scene.vegetation.emplace();
        vegetation.mode = VegetationMode::TerrainRules;
        vegetation.seed = seed.get<std::uint32_t>();
        vegetation.density = ReadNumber(settings.at("density"), 0.0f, 1.0f);
        if (scene.canUseGltfProps && settings.contains("min_plant_y_m")) {
            vegetation.minPlantHeight = ReadNumber(settings.at("min_plant_y_m"), -HeightField::kHeightLimit,
                                                   HeightField::kHeightLimit);
        }
    }

    const auto& spawn = doc.at("spawn");
    const auto& xz = spawn.at(terrain ? "world_xz" : "ground_xz");
    RequireArray(xz, 2);
    const float limitX = terrain ? scene.terrain->Grid().WalkLimit() : WalkSpawn::kLimitX;
    const float limitZ = terrain ? scene.terrain->Grid().WalkLimit() : WalkSpawn::kLimitZ;
    scene.spawn = WalkSpawn{{ReadNumber(xz[0], -limitX, limitX), ReadNumber(xz[1], -limitZ, limitZ)},
                           ReadNumber(spawn.at("yaw_deg"), -180.0f, 180.0f)};

    const auto& camera = doc.at("camera");
    scene.camera = {ReadVec3(camera.at("target")), ReadNumber(camera.at("azimuth_deg")),
                    ReadNumber(camera.at("elevation_deg"), -89.0f, 89.0f),
                    ReadNumber(camera.at("distance_m"), 0.1f, 80.0f)};
    const auto& sun = doc.at("sun");
    scene.sun = {ReadNumber(sun.at("azimuth_deg"), -180.0f, 180.0f),
                 ReadNumber(sun.at("elevation_deg"), 0.0f, 90.0f),
                 ReadNumber(sun.at("intensity"), 0.0f, 2.0f), ReadVec3(sun.at("color"), 0.0f, 1.0f)};
    const auto& shadow = doc.at("shadows");
    scene.shadows = ShadowSettings{shadow.at("enabled").get<bool>(), shadow.at("pcf").get<bool>(),
                                   ReadNumber(shadow.at("depth_bias"), 0.0f, ShadowSettings::kMaxDepthBias)};
    const auto& environment = doc.at("environment");
    const auto& mode = environment.at("sky");
    if (mode != "cubemap" && mode != "panorama") {
        throw std::runtime_error("Unknown scene sky mode.");
    }
    scene.environment = EnvironmentSettings{mode == "cubemap" ? SkyMode::Cubemap : SkyMode::Panorama,
                                            environment.at("car_reflection").get<bool>()};
    return scene;
}

//————————————————————————————————————————————————————————————————————————————————
// 在目标旁创建独立暂存目录。目录创建负责处理同时保存时可能出现的重名。
//————————————————————————————————————————————————————————————————————————————————
std::filesystem::path CreateStagingDirectory(const std::filesystem::path& target) {
    static std::atomic<unsigned> sequence{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    for (;;) {
        auto staging = target;
        staging += ".saving-" + std::to_string(ticks) + "-" + std::to_string(sequence++);
        if (std::filesystem::create_directory(staging)) {
            return staging;
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 先关闭同一文件系统上的暂存文件，再用重命名提交完整版本。
//————————————————————————————————————————————————————————————————————————————————
void WriteAtomically(const std::filesystem::path& path, std::string_view data) {
    if (path.filename().empty()) {
        throw std::runtime_error("Choose an output filename.");
    }
    const auto staging = CreateStagingDirectory(path);
    const auto temporary = staging / "data.tmp";
    try {
        std::ofstream output(temporary, std::ios::binary);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        std::filesystem::rename(temporary, path);
    } catch (...) {
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
        std::filesystem::remove(staging, cleanup);
        throw;
    }
    std::error_code cleanup;
    std::filesystem::remove(staging, cleanup);
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 保存前用同一套规则校验，确保写出的状态能重开。
//————————————————————————————————————————————————————————————————————————————————
void SaveScene(const std::filesystem::path& path, const WorldScene& scene) {
    if (scene.terrain && !scene.canSculpt) {
        throw std::runtime_error("Terrain files open in the sculpting chapters.");
    }
    if (!scene.spawn || scene.IsWalking() || scene.IsDriving()) {
        throw std::runtime_error("Stop previewing before saving.");
    }
    const auto doc = Encode(scene);
    Decode(doc);
    WriteAtomically(path, doc.dump(2) + '\n');
}

//————————————————————————————————————————————————————————————————————————————————
// 单次赋值才修改编辑器与渲染器共享的场景，读取失败时保留原地址和数据。
//————————————————————————————————————————————————————————————————————————————————
void LoadScene(const std::filesystem::path& path, WorldScene& scene) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open scene file.");
    }
    auto loaded = Decode(Json::parse(input));
    if (loaded.terrain.has_value() != scene.terrain.has_value() || (loaded.terrain && !scene.canSculpt)) {
        throw std::runtime_error("Choose a scene version that matches this chapter.");
    }
    if (loaded.road && !scene.road) {
        throw std::runtime_error("Road scenes open in Chapter 09 or later.");
    }
    if (loaded.canPlaceProps && !scene.canPlaceProps) {
        throw std::runtime_error("Prop scenes open in Chapter 10 or later.");
    }
    if (loaded.canUseGltfProps && !scene.canUseGltfProps) {
        throw std::runtime_error("Village scenes open in Chapter 13 or later.");
    }
    if (loaded.lightmapEnabled && !scene.lightmapEnabled) {
        throw std::runtime_error("Lightmap scenes open in Chapter 17 or later.");
    }
    loaded.lightmapEnabled = scene.lightmapEnabled;
    if (loaded.dayCycle && !scene.dayCycle) {
        throw std::runtime_error("Day-cycle scenes open in Chapter 18 or later.");
    }
    if (!loaded.dayCycle) loaded.dayCycle = scene.dayCycle;
    loaded.canUseGltfProps = scene.canUseGltfProps;
    if (scene.road && !loaded.road) {
        loaded.road.emplace();
    }
    if (loaded.paint && !scene.paint) {
        throw std::runtime_error("Painted terrain scenes open in Chapter 08 or later.");
    }
    if (scene.paint && !loaded.paint) {
        loaded.paint.emplace();
    }
    loaded.showChunkBorders = scene.showChunkBorders;
    loaded.terrainPreview = scene.terrainPreview;
    if (loaded.terrain && loaded.terrain->Grid() == TerrainGrid(true) && !scene.capstone) {
        throw std::runtime_error("The expanded terrain opens in Chapter 21.");
    }
    loaded.capstone = scene.capstone;
    loaded.streamTerrain = scene.streamTerrain;
    loaded.materials = scene.materials;
    loaded.water = scene.water;
    loaded.atmosphere = scene.atmosphere;
    loaded.canPlaceProps = scene.canPlaceProps;
    if (loaded.vegetation && (!scene.vegetation || scene.vegetation->mode != VegetationMode::TerrainRules)) {
        throw std::runtime_error("Ecology scenes open in Chapter 12 or later.");
    }
    if (scene.vegetation) {
        if (!loaded.vegetation) loaded.vegetation = VegetationSettings{scene.vegetation->mode};
        loaded.vegetation->visible = scene.vegetation->visible;
        loaded.vegetation->instanced = scene.vegetation->instanced;
    }
    scene = std::move(loaded);
}

//————————————————————————————————————————————————————————————————————————————————
// 文件第一个像素对应样区的最小 X、Z，依次写完一行再增大 Z。
//————————————————————————————————————————————————————————————————————————————————
void ExportRoadMask(const std::filesystem::path& path, const RoadSpline& road, TerrainGrid grid) {
    const auto mask = MakeRoadMask(road, grid);
    const auto side = std::to_string(mask.Side());
    const auto min = std::to_string(-grid.HalfExtent());
    const auto spacing = std::to_string(RoadMask::kSpacing);
    std::string data = "P5\n# TinyWorld world_xz_min=" + min + "," + min +
        " pixel_spacing_m=" + spacing + "\n" + side + " " + side + "\n255\n";
    data.append(reinterpret_cast<const char*>(mask.pixels.data()), mask.pixels.size());
    WriteAtomically(path, data);
}

}  // namespace tinyworld
