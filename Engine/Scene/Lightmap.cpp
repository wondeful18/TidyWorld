#include "Engine/Scene/Lightmap.h"

#include <array>
#include <fstream>
#include <stdexcept>

namespace tinyworld {
namespace {

using Header = std::array<std::uint8_t, 16>;
constexpr Header kHeader{'T', 'W', 'L', 'M', 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0};
constexpr auto kBytes = kPorchLightmapSide * kPorchLightmapSide * 4;
static_assert(kPorchLightmapSide == 256);

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 固定尺寸课程格式。完整验证头部和长度后才把像素交给上传端。
//————————————————————————————————————————————————————————————————————————————————
GltfImage ReadLightmap(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Header header{};
    GltfImage image{kPorchLightmapSide, kPorchLightmapSide, std::vector<std::uint8_t>(kBytes)};
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size()) || header != kHeader ||
        !input.read(reinterpret_cast<char*>(image.pixels.data()), kBytes) ||
        input.peek() != std::char_traits<char>::eof() || input.bad()) {
        throw std::runtime_error("Invalid lightmap: " + path.string());
    }
    return image;
}

//————————————————————————————————————————————————————————————————————————————————
// 写完头和像素再检查流状态，保留磁盘写入失败的错误。
//————————————————————————————————————————————————————————————————————————————————
void WriteLightmap(const std::filesystem::path& path, const GltfImage& image) {
    if (image.width != kPorchLightmapSide || image.height != kPorchLightmapSide || image.pixels.size() != kBytes) {
        throw std::runtime_error("Invalid lightmap dimensions: " + path.string());
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(kHeader.data()), kHeader.size());
    output.write(reinterpret_cast<const char*>(image.pixels.data()), kBytes);
    output.close();
    if (!output) throw std::runtime_error("Cannot write lightmap: " + path.string());
}

}  // namespace tinyworld
