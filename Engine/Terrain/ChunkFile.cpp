#include "Engine/Terrain/ChunkFile.h"

#include "Engine/Terrain/TerrainVisibility.h"

#include <lz4.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace tinyworld {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kMaxBody = 8 * 1024 * 1024;
constexpr std::size_t kSections = 4;
constexpr std::size_t kHeader = 4 + 4 * (8 + 2 * kSections);

//————————————————————————————————————————————————————————————————————————————————
// 明确按小端顺序写入 32 位整数和 IEEE 754 浮点数。
//————————————————————————————————————————————————————————————————————————————————
void Write32(std::string& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>(value >> shift));
}
void WriteFloat(std::string& out, float value) {
    Write32(out, std::bit_cast<std::uint32_t>(value));
}

//————————————————————————————————————————————————————————————————————————————————
// 每段独立读到自己的末尾，损坏的偏移无法越界进入下一段。
//————————————————————————————————————————————————————————————————————————————————
struct Reader {
    std::string_view remaining;  // 当前段还没有消费的字节

    std::string_view Read(std::size_t size) {
        if (size > remaining.size()) throw std::runtime_error("Truncated TWCK section.");
        const auto result = remaining.substr(0, size);
        remaining.remove_prefix(size);
        return result;
    }

    std::uint32_t Integer() {
        const auto bytes = Read(4);
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{static_cast<std::uint8_t>(bytes[i])} << (8 * i);
        return value;
    }

    float Float() {
        const auto value = std::bit_cast<float>(Integer());
        if (!std::isfinite(value)) throw std::runtime_error("Non-finite TWCK value.");
        return value;
    }
};

struct Section {
    std::uint32_t offset{};  // 解压后块体内的字节偏移
    std::uint32_t size{};    // 段的字节数
};

//————————————————————————————————————————————————————————————————————————————————
// 按稳定资产 ID 保存变换，只允许该原型自带的烘焙贴图。
//————————————————————————————————————————————————————————————————————————————————
void WriteProp(std::string& body, const PlacedProp& prop) {
    const std::string_view id = kPropAssets.at(prop.asset).id;
    Write32(body, static_cast<std::uint32_t>(id.size()));
    body += id;
    for (const auto value : {prop.transform.position, prop.transform.rotationDegrees, prop.transform.scale}) {
        WriteFloat(body, value.x);
        WriteFloat(body, value.y);
        WriteFloat(body, value.z);
    }
    Write32(body, prop.lightmap.empty() ? 0 : 1);
}

PlacedProp ReadProp(Reader& data) {
    const auto id = data.Read(data.Integer());
    const auto asset = std::find_if(kPropAssets.begin(), kPropAssets.end(),
                                    [&](const PropAsset& item) { return id == item.id; });
    if (asset == kPropAssets.end()) throw std::runtime_error("Unknown TWCK asset ID.");
    const auto vector = [&] { return glm::vec3{data.Float(), data.Float(), data.Float()}; };
    const Transform transform{vector(), vector(), vector()};
    const auto baked = data.Integer();
    if (baked > 1 || (baked && !asset->lightmap) || transform.scale.x <= 0 ||
        transform.scale.y <= 0 || transform.scale.z <= 0) throw std::runtime_error("Invalid TWCK prop.");
    return {static_cast<std::size_t>(asset - kPropAssets.begin()), transform, baked ? asset->lightmap : ""};
}

//————————————————————————————————————————————————————————————————————————————————
// 根据固定头列出的范围创建视图，并验证完整覆盖未压缩的块体。
//————————————————————————————————————————————————————————————————————————————————
auto ReadSections(Reader& header, std::uint32_t size) {
    std::array<Section, kSections> result;
    std::uint32_t end = 0;
    for (auto& section : result) {
        section = {header.Integer(), header.Integer()};
        if (section.offset != end || section.size > size - end) {
            throw std::runtime_error("Invalid TWCK section offsets.");
        }
        end += section.size;
    }
    if (end != size) throw std::runtime_error("Invalid TWCK body length.");
    return result;
}

}  // namespace

std::filesystem::path ChunkPath(const std::filesystem::path& world, glm::ivec2 key) {
    std::array<char, 32> name{};
    std::snprintf(name.data(), name.size(), "c_%+03d_%+03d.twchunk", key.x, key.y);
    return world / "chunks" / name.data();
}

std::string EncodeChunk(const ChunkTile& tile, glm::ivec2 key, bool compress) {
    std::string body;
    std::array<Section, kSections> sections;
    const auto section = [&](std::size_t index, const auto& write) {
        const auto start = body.size();
        write();
        sections[index] = {static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(body.size() - start)};
    };
    section(0, [&] { for (float height : tile.heights.Values()) WriteFloat(body, height); });
    section(1, [&] {
        for (const auto& pixel : tile.splat) for (auto channel : pixel) body.push_back(static_cast<char>(channel));
    });
    section(2, [&] { for (const auto& prop : tile.props) WriteProp(body, prop); });
    section(3, [&] {
        for (const auto& vertex : tile.road) {
            for (const auto& attribute : {vertex.position, vertex.normal, vertex.color}) {
                for (float value : attribute) WriteFloat(body, value);
            }
        }
    });
    if (body.empty() || body.size() > kMaxBody) throw std::runtime_error("Oversized TWCK body.");
    std::string encoded;
    if (compress) {
        encoded.resize(LZ4_compressBound(static_cast<int>(body.size())));
        const int bytes = LZ4_compress_default(body.data(), encoded.data(),
                                                static_cast<int>(body.size()), static_cast<int>(encoded.size()));
        if (bytes > 0 && static_cast<std::size_t>(bytes) < body.size()) encoded.resize(bytes);
        else encoded.clear();
    }

    const auto bounds = MeasureTerrainBounds(tile.heights, key);
    std::string file = "TWCK";
    Write32(file, kVersion);
    Write32(file, std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(key.x)));
    Write32(file, std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(key.y)));
    WriteFloat(file, bounds.low.y);
    WriteFloat(file, bounds.high.y);
    Write32(file, encoded.empty() ? 0 : 1);
    Write32(file, static_cast<std::uint32_t>(body.size()));
    Write32(file, static_cast<std::uint32_t>(encoded.empty() ? body.size() : encoded.size()));
    for (const auto value : sections) {
        Write32(file, value.offset);
        Write32(file, value.size);
    }
    file += encoded.empty() ? body : encoded;
    return file;
}

ChunkTile ReadChunk(const std::filesystem::path& path, TerrainGrid grid, glm::ivec2 key, ChunkReadTimes& times) {
    try {
        auto start = Clock::now();
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto length = input.tellg();
        if (length < static_cast<std::streamoff>(kHeader) || length > static_cast<std::streamoff>(kHeader + kMaxBody)) {
            throw std::runtime_error("Invalid TWCK file length.");
        }
        std::string file(static_cast<std::size_t>(length), char{});
        input.seekg(0);
        if (!input.read(file.data(), static_cast<std::streamsize>(length))) throw std::runtime_error("Cannot read TWCK data.");
        times.diskMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

        Reader header{file};
        if (header.Read(4) != "TWCK" || header.Integer() != kVersion ||
            std::bit_cast<std::int32_t>(header.Integer()) != key.x ||
            std::bit_cast<std::int32_t>(header.Integer()) != key.y) {
            throw std::runtime_error("Invalid TWCK magic, version or key.");
        }
        const float minY = header.Float();
        const float maxY = header.Float();
        const auto codec = header.Integer();
        const auto size = header.Integer();
        const auto stored = header.Integer();
        const auto sections = ReadSections(header, size);
        if (maxY < minY || codec > 1 || size > kMaxBody || stored != header.remaining.size() ||
            (codec == 0 && stored != size)) throw std::runtime_error("Invalid TWCK header.");

        std::string decompressed;
        std::string_view body = header.remaining;
        if (codec == 1) {
            start = Clock::now();
            decompressed.resize(size);
            if (LZ4_decompress_safe(body.data(), decompressed.data(), static_cast<int>(stored),
                                    static_cast<int>(size)) != static_cast<int>(size)) {
                throw std::runtime_error("Cannot decompress TWCK body.");
            }
            times.decompressMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            body = decompressed;
        }
        const auto part = [&](std::size_t i) { return Reader{body.substr(sections[i].offset, sections[i].size)}; };
        const auto haloSide = grid.Cells() + 3;
        HeightField::Samples halo(haloSide * haloSide);
        auto data = part(0);
        if (data.remaining.size() != halo.size() * sizeof(float)) throw std::runtime_error("Invalid TWCK halo size.");
        for (auto& h : halo) h = data.Float();

        ChunkTile tile{HeightField(grid, key, std::move(halo)), {}, {}, {}};
        const auto bounds = MeasureTerrainBounds(tile.heights, key);
        if (bounds.low.y != minY || bounds.high.y != maxY) throw std::runtime_error("TWCK height bounds do not match.");
        data = part(1);
        tile.splat.resize(kSplatTileSide * kSplatTileSide);
        if (data.remaining.size() != tile.splat.size() * 4) throw std::runtime_error("Invalid TWCK splat size.");
        for (auto& pixel : tile.splat) for (auto& channel : pixel) channel = static_cast<std::uint8_t>(data.Read(1)[0]);

        data = part(2);
        while (!data.remaining.empty()) {
            if (tile.props.size() >= PlacedProp::kLimit) throw std::runtime_error("Too many props in TWCK tile.");
            auto prop = ReadProp(data);
            const auto p = prop.transform.position;
            if (grid.KeyAt({p.x, p.z}) != key) throw std::runtime_error("TWCK prop lies in the wrong tile.");
            tile.props.push_back(std::move(prop));
        }

        data = part(3);
        constexpr std::size_t triangleBytes = 3 * 9 * sizeof(float);
        if (data.remaining.size() % triangleBytes != 0) throw std::runtime_error("Invalid TWCK road triangles.");
        tile.road.resize(data.remaining.size() / (9 * sizeof(float)));
        for (auto& vertex : tile.road) {
            for (auto* attribute : {&vertex.position, &vertex.normal, &vertex.color}) {
                for (auto& value : *attribute) value = data.Float();
            }
        }
        return tile;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

}  // namespace tinyworld
