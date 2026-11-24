#include "Editor/Application.h"

#include <charconv>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

namespace {

//————————————————————————————————————————————————————————————————————————————————
// 读取完整的数值并检查范围。浮点 NaN 不满足两个边界比较。
//————————————————————————————————————————————————————————————————————————————————
bool ParseFloatInRange(std::string_view text, float& value, float minimum, float maximum) {
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end && value >= minimum && value <= maximum;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 读取章节和输出选项，并把失败报告为非零进程退出码。
//————————————————————————————————————————————————————————————————————————————————
int main(int argc, char** argv) {
    tinyworld::ApplicationOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--direct") {
            options.direct = true;
        } else if (argument == "--smoke-test") {
            options.smokeTest = true;
        } else if (argument == "--benchmark-terrain") {
            options.benchmarkTerrain = true;
        } else if (argument == "--benchmark-vegetation") {
            options.benchmarkVegetation = true;
        } else if (argument == "--chunk-borders") {
            options.chunkBorders = true;
        } else if (argument == "--lod-preview") {
            options.lodPreview = true;
        } else if (argument == "--sculpt-tools") {
            options.sculptTools = true;
        } else if (argument == "--capture" && index + 1 < argc) {
            options.capturePath = argv[++index];
        } else if (argument == "--scene" && index + 1 < argc) {
            options.scenePath = argv[++index];
        } else if (argument == "--export-road-mask" && index + 1 < argc) {
            options.roadMaskPath = argv[++index];
        } else if (argument == "--chapter" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (value == "01") {
                options.chapter = tinyworld::Chapter::Triangle;
            } else if (value == "02") {
                options.chapter = tinyworld::Chapter::World;
            } else if (value == "03") {
                options.chapter = tinyworld::Chapter::SunShadows;
            } else if (value == "04") {
                options.chapter = tinyworld::Chapter::Sky;
            } else if (value == "05") {
                options.chapter = tinyworld::Chapter::Walk;
            } else if (value == "06") {
                options.chapter = tinyworld::Chapter::Terrain;
            } else if (value == "07") {
                options.chapter = tinyworld::Chapter::Sculpt;
            } else if (value == "08") {
                options.chapter = tinyworld::Chapter::Materials;
            } else if (value == "09") {
                options.chapter = tinyworld::Chapter::Road;
            } else if (value == "10") {
                options.chapter = tinyworld::Chapter::Props;
            } else if (value == "11") {
                options.chapter = tinyworld::Chapter::Meadow;
            } else if (value == "12") {
                options.chapter = tinyworld::Chapter::Ecology;
            } else if (value == "13") {
                options.chapter = tinyworld::Chapter::Village;
            } else if (value == "14") {
                options.chapter = tinyworld::Chapter::Water;
            } else if (value == "15") {
                options.chapter = tinyworld::Chapter::Coast;
            } else if (value == "16") {
                options.chapter = tinyworld::Chapter::Atmosphere;
            } else if (value == "17") {
                options.chapter = tinyworld::Chapter::Lightmap;
            } else if (value == "18") {
                options.chapter = tinyworld::Chapter::DayCycle;
            } else if (value == "19") {
                options.chapter = tinyworld::Chapter::TerrainVisibility;
            } else {
                std::cerr << "Choose chapter 01 through 19.\n";
                return EXIT_FAILURE;
            }
        } else if (argument == "--preset" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (value == "overlap") {
                options.worldPreset = tinyworld::WorldPreset::Overlap;
            } else if (value == "evening") {
                options.worldPreset = tinyworld::WorldPreset::Evening;
            } else if (value == "drive") {
                options.worldPreset = tinyworld::WorldPreset::Drive;
            } else if (value == "walk") {
                options.worldPreset = tinyworld::WorldPreset::Walk;
            } else if (value == "materials") {
                options.worldPreset = tinyworld::WorldPreset::Materials;
            } else if (value == "shore") {
                options.worldPreset = tinyworld::WorldPreset::Shore;
            } else if (value == "porch") {
                options.worldPreset = tinyworld::WorldPreset::Porch;
            } else {
                std::cerr << "Choose preset overlap, evening, drive, walk, materials, shore or porch.\n";
                return EXIT_FAILURE;
            }
        } else if (argument == "--no-shadows" || argument == "--hard-shadows" || argument == "--shadow-bias") {
            if (!options.shadowOverrides) {
                options.shadowOverrides.emplace();
            }
            auto& shadows = *options.shadowOverrides;
            if (argument == "--no-shadows") {
                shadows.enabled = false;
            } else if (argument == "--hard-shadows") {
                shadows.pcf = false;
            } else if (index + 1 == argc || !ParseFloatInRange(argv[++index], shadows.depthBias, 0.0f,
                                          tinyworld::ShadowSettings::kMaxDepthBias)) {
                std::cerr << "Shadow bias must be a number between 0 and 0.01.\n";
                return EXIT_FAILURE;
            }
        } else if (argument == "--sky" || argument == "--no-reflections") {
            if (!options.environmentOverrides) {
                options.environmentOverrides.emplace();
            }
            auto& environment = *options.environmentOverrides;
            if (argument == "--no-reflections") {
                environment.carReflection = false;
            } else {
                const std::string_view mode = index + 1 < argc ? argv[++index] : "";
                if (mode == "cubemap") {
                    environment.sky = tinyworld::SkyMode::Cubemap;
                } else if (mode == "panorama") {
                    environment.sky = tinyworld::SkyMode::Panorama;
                } else {
                    std::cerr << "Choose sky cubemap or panorama.\n";
                    return EXIT_FAILURE;
                }
            }
        } else if (argument == "--water" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (value == "lit") {
                options.waterView = tinyworld::WaterView::Lit;
            } else if (value == "depth") {
                options.waterView = tinyworld::WaterView::Depth;
            } else if (value == "no-reflection") {
                options.waterView = tinyworld::WaterView::NoReflection;
            } else if (value == "off") {
                options.waterView = tinyworld::WaterView::Off;
            } else {
                std::cerr << "Choose water lit, depth, no-reflection or off.\n";
                return EXIT_FAILURE;
            }
        } else if (argument == "--water-time" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            const auto* end = value.data() + value.size();
            float seconds{};
            const auto parsed = std::from_chars(value.data(), end, seconds);
            if (parsed.ec != std::errc{} || parsed.ptr != end ||
                !(seconds >= 0.0f && seconds < tinyworld::WaterSettings::kLoopSeconds)) {
                std::cerr << "Water time must be in [0, 32) seconds.\n";
                return EXIT_FAILURE;
            }
            options.waterTime = seconds;
        } else if ((argument == "--terrain-lod" || argument == "--terrain-cull") && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (value != "on" && value != "off") {
                std::cerr << "Choose terrain option on or off.\n";
                return EXIT_FAILURE;
            }
            auto& option = argument == "--terrain-lod" ? options.terrainLod : options.terrainCull;
            option = value == "on";
        } else if (argument == "--terrain-view" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (value.size() != 1 || value.front() < '0' || value.front() > '3') {
                std::cerr << "Choose terrain view 0 through 3.\n";
                return EXIT_FAILURE;
            }
            options.terrainView = static_cast<std::size_t>(value.front() - '0');
        } else if (argument == "--time" && index + 1 < argc) {
            float hour{};
            if (!ParseFloatInRange(argv[++index], hour, 0.0f, 24.0f)) {
                std::cerr << "Time must be in [0, 24] hours.\n";
                return EXIT_FAILURE;
            }
            options.dayHour = hour;
        } else if (argument == "--lightmap" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (value != "on" && value != "off") {
                std::cerr << "Choose lightmap on or off.\n";
                return EXIT_FAILURE;
            }
            options.lightmapEnabled = value == "on";
        } else if (argument == "--fog" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (value != "on" && value != "off") {
                std::cerr << "Choose fog on or off.\n";
                return EXIT_FAILURE;
            }
            options.fogEnabled = value == "on";
        } else if (argument == "--shadow-distance" && index + 1 < argc) {
            float metres{};
            if (!ParseFloatInRange(argv[++index], metres, tinyworld::AtmosphereSettings::kMinShadowDistance,
                                   tinyworld::AtmosphereSettings::kMaxShadowDistance)) {
                std::cerr << "Shadow distance must be between 8 and 85 metres.\n";
                return EXIT_FAILURE;
            }
            options.shadowDistance = metres;
        } else if (argument == "--help") {
            std::cout << "TinyWorld [--chapter 01..19] [--preset overlap|evening|drive|walk|materials|shore|porch] [--direct]\n"
                         "          [--smoke-test | --capture output.ppm]\n"
                         "Chapter 03+: [--no-shadows] [--hard-shadows] [--shadow-bias 0..0.01]\n"
                         "Chapter 04+: [--sky cubemap|panorama] [--no-reflections]\n"
                         "Chapter 05: [--scene saved.json] [--preset walk]\n"
                         "Chapter 06+: [--chunk-borders] [--preset walk]\n"
                         "Chapter 06: [--lod-preview]\n"
                         "Chapter 07+: [--scene valley.json] [--sculpt-tools]\n"
                         "Chapter 08+: [--preset materials]\n"
                         "Chapter 09+: [--export-road-mask output.pgm]\n"
                         "Chapter 11+: [--benchmark-vegetation]\n"
                         "Chapter 14+: [--water lit|depth|no-reflection|off] [--water-time seconds]\n"
                         "Chapter 15+: [--preset shore]\n"
                         "Chapter 16+: [--fog on|off] [--shadow-distance metres]\n"
                         "Chapter 17+: [--lightmap on|off] [--preset porch]\n"
                         "Chapter 18+: [--time hours]\n"
                         "Chapter 19+: [--terrain-lod on|off] [--terrain-cull on|off] [--terrain-view 0..3]\n"
                         "Chapter 19: [--benchmark-terrain]\n";
            return EXIT_SUCCESS;
        } else {
            std::cerr << "Unknown option: " << argument << " (use --help)\n";
            return EXIT_FAILURE;
        }
    }
    if (options.smokeTest && !options.capturePath.empty()) {
        std::cerr << "Choose either --smoke-test or --capture.\n";
        return EXIT_FAILURE;
    }
    if (!options.roadMaskPath.empty() &&
        (options.chapter < tinyworld::Chapter::Road || options.smokeTest || !options.capturePath.empty())) {
        std::cerr << "Export a road mask with chapter 09 or later in a separate command.\n";
        return EXIT_FAILURE;
    }
    if (options.benchmarkVegetation &&
        (options.chapter < tinyworld::Chapter::Meadow || options.smokeTest || !options.capturePath.empty() ||
         !options.roadMaskPath.empty() || options.worldPreset == tinyworld::WorldPreset::Materials)) {
        std::cerr << "Run the vegetation benchmark separately with chapter 11 or 12 and a terrain view.\n";
        return EXIT_FAILURE;
    }
    if (options.chapter == tinyworld::Chapter::Triangle && options.worldPreset != tinyworld::WorldPreset::Default) {
        std::cerr << "Scene presets require chapter 02 or later.\n";
        return EXIT_FAILURE;
    }
    if (options.shadowOverrides && options.chapter < tinyworld::Chapter::SunShadows) {
        std::cerr << "Shadow options require chapter 03 or later.\n";
        return EXIT_FAILURE;
    }
    if ((options.waterView || options.waterTime) && options.chapter < tinyworld::Chapter::Water) {
        std::cerr << "Water options require chapter 14.\n";
        return EXIT_FAILURE;
    }
    if ((options.terrainLod || options.terrainCull || options.terrainView) &&
        options.chapter < tinyworld::Chapter::TerrainVisibility) {
        std::cerr << "Terrain visibility options require chapter 19 or later.\n";
        return EXIT_FAILURE;
    }
    if (options.benchmarkTerrain &&
        (options.chapter != tinyworld::Chapter::TerrainVisibility || options.smokeTest ||
         options.benchmarkVegetation || !options.capturePath.empty() || !options.roadMaskPath.empty() ||
         options.worldPreset != tinyworld::WorldPreset::Default || options.terrainView ||
         options.terrainCull || options.terrainLod)) {
        std::cerr << "Run the chapter 19 terrain benchmark separately with its default route and toggles.\n";
        return EXIT_FAILURE;
    }
    if (options.terrainView && options.worldPreset != tinyworld::WorldPreset::Default) {
        std::cerr << "Choose either a terrain view or a scene preset.\n";
        return EXIT_FAILURE;
    }
    if (options.dayHour && options.chapter < tinyworld::Chapter::DayCycle) {
        std::cerr << "Time options require chapter 18 or later.\n";
        return EXIT_FAILURE;
    }
    if ((options.lightmapEnabled || options.worldPreset == tinyworld::WorldPreset::Porch) &&
        options.chapter < tinyworld::Chapter::Lightmap) {
        std::cerr << "Lightmap options require chapter 17 or later.\n";
        return EXIT_FAILURE;
    }
    if ((options.fogEnabled || options.shadowDistance) && options.chapter < tinyworld::Chapter::Atmosphere) {
        std::cerr << "Fog and shadow distance options require chapter 16.\n";
        return EXIT_FAILURE;
    }
    if (options.environmentOverrides && options.chapter < tinyworld::Chapter::Sky) {
        std::cerr << "Sky and reflection options require chapter 04 or later.\n";
        return EXIT_FAILURE;
    }
    if (!options.scenePath.empty() && options.chapter != tinyworld::Chapter::Walk &&
        options.chapter < tinyworld::Chapter::Sculpt) {
        std::cerr << "Scene files require chapter 05 or 07 and later.\n";
        return EXIT_FAILURE;
    }
    if (options.worldPreset == tinyworld::WorldPreset::Walk && options.chapter < tinyworld::Chapter::Walk) {
        std::cerr << "The walk preset requires chapter 05 or later.\n";
        return EXIT_FAILURE;
    }
    if (options.worldPreset == tinyworld::WorldPreset::Shore && options.chapter < tinyworld::Chapter::Coast) {
        std::cerr << "The shore preset requires chapter 15 or later.\n";
        return EXIT_FAILURE;
    }
    if (options.worldPreset == tinyworld::WorldPreset::Materials && options.chapter < tinyworld::Chapter::Materials) {
        std::cerr << "Material samples require chapter 08 or later.\n";
        return EXIT_FAILURE;
    }
    if (options.chunkBorders && options.chapter < tinyworld::Chapter::Terrain) {
        std::cerr << "Chunk borders require chapter 06 or later.\n";
        return EXIT_FAILURE;
    }
    if ((options.lodPreview && options.chapter != tinyworld::Chapter::Terrain) ||
        (options.sculptTools && options.chapter < tinyworld::Chapter::Sculpt)) {
        std::cerr << "LOD preview is for chapter 06 and sculpt tools require chapter 07 or later.\n";
        return EXIT_FAILURE;
    }
    if (options.chapter >= tinyworld::Chapter::Terrain && options.worldPreset == tinyworld::WorldPreset::Drive) {
        std::cerr << "Use --preset walk to explore the terrain chapters.\n";
        return EXIT_FAILURE;
    }
    if (!options.scenePath.empty() && (options.worldPreset == tinyworld::WorldPreset::Overlap ||
                                       options.worldPreset == tinyworld::WorldPreset::Evening)) {
        std::cerr << "Choose a roaming or material preset to view a saved scene.\n";
        return EXIT_FAILURE;
    }
    try {
        return tinyworld::RunApplication(options);
    } catch (const std::exception& error) {
        std::cerr << "[FATAL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
