#include "Engine/Scene/GltfAsset.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <stb_image.h>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <string_view>

namespace tinyworld {
namespace {

constexpr std::size_t kMaxFileBytes = 16 * 1024 * 1024;
constexpr int kMaxImageSide = 2048;
constexpr std::size_t kMaxVertices = 300000;

//————————————————————————————————————————————————————————————————————————————————
// 先检查磁盘大小，限制课程资产的单个二进制源文件，再一次读完。
//————————————————————————————————————————————————————————————————————————————————
std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(kMaxFileBytes)) {
        throw std::runtime_error("Cannot open asset or asset exceeds 16 MiB.");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!input.seekg(0).read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("Cannot read asset file.");
    }
    return bytes;
}

//————————————————————————————————————————————————————————————————————————————————
// 外部数据从模型目录读取。只接受无需 URI 转义的相对本地路径。
//————————————————————————————————————————————————————————————————————————————————
std::filesystem::path ResolveUri(const std::filesystem::path& directory, std::string_view uri) {
    if (uri.empty() || uri.find_first_of(":%\\?#") != std::string_view::npos) {
        throw std::runtime_error("Expected a local, unescaped relative URI.");
    }
    const auto root = std::filesystem::canonical(directory);
    const auto resolved = std::filesystem::canonical(root / std::filesystem::path(std::u8string(uri.begin(), uri.end())));
    const auto local = resolved.lexically_relative(root);
    if (local.empty() || local.is_absolute() || *local.begin() == "..") {
        throw std::runtime_error("External glTF data must stay inside the asset directory.");
    }
    return resolved;
}

//————————————————————————————————————————————————————————————————————————————————
// 同一套 RGBA 数据既可上传为底色纹理，也可保留线性材质通道。
//————————————————————————————————————————————————————————————————————————————————
GltfImage ReadImage(const cgltf_texture_view& view, const std::filesystem::path& directory) {
    if (!view.texture) return {};
    if (view.texcoord != 0 || view.has_transform || !view.texture->image ||
        view.texture->has_basisu || view.texture->has_webp) {
        throw std::runtime_error("Expected an ordinary PNG/JPEG on TEXCOORD_0.");
    }
    if (const auto* sampler = view.texture->sampler;
        sampler && (sampler->wrap_s != cgltf_wrap_mode_repeat || sampler->wrap_t != cgltf_wrap_mode_repeat ||
                    (sampler->mag_filter && sampler->mag_filter != cgltf_filter_type_linear) ||
                    (sampler->min_filter && sampler->min_filter != cgltf_filter_type_linear))) {
        throw std::runtime_error("Expected repeat wrapping and linear filtering without mipmaps.");
    }
    const auto& image = *view.texture->image;
    std::vector<std::uint8_t> external;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    if (image.buffer_view) {
        data = cgltf_buffer_view_data(image.buffer_view);
        size = image.buffer_view->size;
    } else if (image.uri) {
        external = ReadBytes(ResolveUri(directory, image.uri));
        data = external.data();
        size = external.size();
    }
    int width = 0, height = 0, channels = 0;
    const bool png = size >= 8 && data && data[0] == 0x89 && data[1] == 'P' &&
                     data[2] == 'N' && data[3] == 'G';
    const bool jpeg = size >= 2 && data && data[0] == 0xff && data[1] == 0xd8;
    if ((!png && !jpeg) || size > kMaxFileBytes ||
        !stbi_info_from_memory(data, static_cast<int>(size), &width, &height, &channels) ||
        width > kMaxImageSide || height > kMaxImageSide) {
        throw std::runtime_error("Invalid PNG/JPEG or image exceeds 2048 pixels per side.");
    }
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4), &stbi_image_free);
    if (!pixels) throw std::runtime_error("Cannot decode glTF image.");
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
            {pixels.get(), pixels.get() + static_cast<std::size_t>(width) * height * 4}};
}

//————————————————————————————————————————————————————————————————————————————————
// 仅导入不透明金属度／粗糙度材质。纹理系数按 glTF 缺省值保留。
//————————————————————————————————————————————————————————————————————————————————
GltfMaterial ReadMaterial(const cgltf_material& source, const std::filesystem::path& directory) {
    if (source.alpha_mode != cgltf_alpha_mode_opaque || source.double_sided || source.extensions_count ||
        source.normal_texture.texture || source.occlusion_texture.texture || source.emissive_texture.texture ||
        source.emissive_factor[0] || source.emissive_factor[1] || source.emissive_factor[2] ||
        source.has_pbr_specular_glossiness || source.unlit) {
        throw std::runtime_error("Expected a single-sided opaque metallic-roughness material.");
    }
    const auto& pbr = source.pbr_metallic_roughness;
    const auto unitRange = [](float value) { return value >= 0.0f && value <= 1.0f; };
    if (!std::all_of(pbr.base_color_factor, pbr.base_color_factor + 4, unitRange) ||
        !unitRange(pbr.metallic_factor) || !unitRange(pbr.roughness_factor)) {
        throw std::runtime_error("Material factors must be finite values between 0 and 1.");
    }
    return {ReadImage(pbr.base_color_texture, directory), ReadImage(pbr.metallic_roughness_texture, directory),
            glm::make_vec3(pbr.base_color_factor), pbr.metallic_factor, pbr.roughness_factor};
}

//————————————————————————————————————————————————————————————————————————————————
// 读取一个网格批次，按节点世界矩阵变换位置，按逆转置矩阵变换法线。
//————————————————————————————————————————————————————————————————————————————————
GltfPrimitive ReadPrimitive(const cgltf_primitive& source, const cgltf_data& model,
                            const glm::mat4& transform, std::size_t& totalVertices) {
    if (source.type != cgltf_primitive_type_triangles || source.targets_count ||
        source.has_draco_mesh_compression || source.mappings_count) {
        throw std::runtime_error("Expected an uncompressed triangle primitive.");
    }
    const auto* position = cgltf_find_accessor(&source, cgltf_attribute_type_position, 0);
    const auto* normal = cgltf_find_accessor(&source, cgltf_attribute_type_normal, 0);
    const auto* uv = cgltf_find_accessor(&source, cgltf_attribute_type_texcoord, 0);
    const auto* bakeUv = cgltf_find_accessor(&source, cgltf_attribute_type_texcoord, 1);
    const bool textured = source.material && (source.material->pbr_metallic_roughness.base_color_texture.texture ||
                                             source.material->pbr_metallic_roughness.metallic_roughness_texture.texture);
    if (!position || position->type != cgltf_type_vec3 || !normal || normal->type != cgltf_type_vec3 ||
        (textured && !uv) || (uv && uv->type != cgltf_type_vec2) ||
        (bakeUv && (bakeUv->type != cgltf_type_vec2 || bakeUv->count != position->count)) ||
        cgltf_find_accessor(&source, cgltf_attribute_type_color, 0)) {
        throw std::runtime_error("Expected POSITION/NORMAL and TEXCOORD_0 for textured meshes, without vertex colors.");
    }
    const auto count = source.indices ? source.indices->count : position->count;
    if (!count || count % 3 || count > kMaxVertices - totalVertices ||
        (source.indices && source.indices->is_sparse)) {
        throw std::runtime_error("Invalid or oversized triangle list, or sparse indices.");
    }
    totalVertices += count;
    const float determinant = glm::determinant(glm::mat3(transform));
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-8f) {
        throw std::runtime_error("Singular node transform.");
    }
    const auto normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    GltfPrimitive result;
    result.material = source.material ? static_cast<std::size_t>(source.material - model.materials) : model.materials_count;
    result.hasLightmapUv = bakeUv != nullptr;
    result.vertices.reserve(std::min(count, position->count));
    result.indices.reserve(count);
    std::unordered_map<std::size_t, std::uint32_t> remap;
    for (std::size_t i = 0; i < count; ++i) {
        // 镜像节点翻转绕序，背面剔除仍与法线一致。
        std::size_t corner = i;
        if (determinant < 0) {
            if (i % 3 == 1) ++corner;
            if (i % 3 == 2) --corner;
        }
        const auto index = source.indices ? cgltf_accessor_read_index(source.indices, corner) : corner;
        const auto [entry, inserted] = remap.try_emplace(index, static_cast<std::uint32_t>(result.vertices.size()));
        result.indices.push_back(entry->second);
        if (!inserted) continue;
        glm::vec3 p{}, n{};
        glm::vec2 texcoord{0.0f}, chart{0.0f};
        if (index >= position->count || !cgltf_accessor_read_float(position, index, glm::value_ptr(p), 3) ||
            !cgltf_accessor_read_float(normal, index, glm::value_ptr(n), 3) ||
            (uv && !cgltf_accessor_read_float(uv, index, glm::value_ptr(texcoord), 2)) ||
            (bakeUv && !cgltf_accessor_read_float(bakeUv, index, glm::value_ptr(chart), 2))) {
            throw std::runtime_error("Cannot read glTF vertex attributes.");
        }
        p = glm::vec3(transform * glm::vec4(p, 1.0f));
        n = normalMatrix * n;
        const float length = glm::length(n);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            !std::isfinite(length) || length < 1.0e-8f ||
            !std::isfinite(texcoord.x) || !std::isfinite(texcoord.y) || !std::isfinite(chart.x) || !std::isfinite(chart.y)) {
            throw std::runtime_error("Expect finite positions, UVs and nonzero normals.");
        }
        n /= length;
        result.vertices.push_back({{{p.x, p.y, p.z}, {n.x, n.y, n.z}, {1.0f, 1.0f, 1.0f}}, texcoord, chart});
    }
    return result;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 解析并校验文件，从默认场景展开静态节点。异常由调用者在启动或导入面板上显示。
//————————————————————————————————————————————————————————————————————————————————
GltfAsset LoadGltfAsset(const std::filesystem::path& path) {
    try {
        const auto bytes = ReadBytes(path);
        const auto directory = path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
        cgltf_options options{};
        std::vector<std::vector<std::uint8_t>> external;
        cgltf_data* parsed = nullptr;
        if (cgltf_parse(&options, bytes.data(), bytes.size(), &parsed) != cgltf_result_success) {
            throw std::runtime_error("Invalid glTF 2.0 document.");
        }
        const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(parsed, &cgltf_free);
        if (!data->asset.version || std::string_view(data->asset.version) != "2.0" ||
            !data->scenes_count || data->extensions_required_count || data->skins_count || data->animations_count ||
            data->materials_count > 64 || data->nodes_count > 1024 || data->buffers_count > 16) {
            throw std::runtime_error("Expected a small static scene with no required extensions.");
        }
        external.resize(data->buffers_count);
        for (std::size_t i = 0; i < data->buffers_count; ++i) {
            auto& buffer = data->buffers[i];
            if (buffer.size > kMaxFileBytes) throw std::runtime_error("glTF buffer exceeds 16 MiB.");
            if (!buffer.uri) continue;
            const std::string_view uri(buffer.uri);
            if (uri.starts_with("data:")) {
                const auto comma = uri.find(',');
                if (comma == uri.npos || uri.size() - comma - 1 < (buffer.size * 8 + 5) / 6) {
                    throw std::runtime_error("Truncated embedded glTF buffer.");
                }
                continue;
            }
            external[i] = ReadBytes(ResolveUri(directory, buffer.uri));
            if (external[i].size() < buffer.size) throw std::runtime_error("Truncated external glTF buffer.");
            buffer.data = external[i].data();
        }
        // 校验尺寸后再调用 cgltf 的索引检查，避免对非法 stride 或偏移做乘法或读取。
        for (std::size_t i = 0; i < data->buffer_views_count; ++i) {
            const auto& view = data->buffer_views[i];
            if (view.has_meshopt_compression || !view.buffer || view.offset > view.buffer->size ||
                view.size > view.buffer->size - view.offset) {
                throw std::runtime_error("Invalid or compressed glTF buffer view.");
            }
        }
        for (std::size_t i = 0; i < data->accessors_count; ++i) {
            const auto& accessor = data->accessors[i];
            const auto element = cgltf_calc_size(accessor.type, accessor.component_type);
            const auto component = cgltf_component_size(accessor.component_type);
            const auto* view = accessor.buffer_view;
            if (!view || accessor.is_sparse || !accessor.count || !element || !component ||
                accessor.stride < element || accessor.stride % component != 0 ||
                accessor.offset > view->size || (view->offset + accessor.offset) % component != 0 ||
                element > view->size - accessor.offset ||
                accessor.count - 1 > (view->size - accessor.offset - element) / accessor.stride) {
                throw std::runtime_error("Invalid accessor, or sparse accessor outside the course subset.");
            }
        }
        if (cgltf_load_buffers(&options, data.get(), nullptr) != cgltf_result_success ||
            cgltf_validate(data.get()) != cgltf_result_success) {
            throw std::runtime_error("Invalid glTF buffers or accessors.");
        }
        for (std::size_t i = 0; i < data->buffers_count; ++i) {
            if (!data->buffers[i].data) throw std::runtime_error("glTF buffer has no backing data.");
        }
        const auto& scene = data->scene ? *data->scene : data->scenes[0];
        GltfAsset result;
        std::size_t imageBytes = 0;
        for (std::size_t i = 0; i < data->materials_count; ++i) {
            auto material = ReadMaterial(data->materials[i], directory);
            imageBytes += material.baseColor.pixels.size() + material.metalRough.pixels.size();
            if (imageBytes > 64 * 1024 * 1024) throw std::runtime_error("Decoded glTF images exceed 64 MiB.");
            result.materials.push_back(std::move(material));
        }
        result.materials.emplace_back();
        std::size_t total = 0;
        const auto visit = [&](auto&& recurse, const cgltf_node& node) -> void {
            if (node.has_mesh_gpu_instancing) throw std::runtime_error("Unsupported glTF instance extension.");
            if (node.mesh) {
                glm::mat4 transform;
                cgltf_node_transform_world(&node, glm::value_ptr(transform));
                for (std::size_t i = 0; i < node.mesh->primitives_count; ++i) {
                    if (result.primitives.size() == 256) throw std::runtime_error("Too many mesh primitives.");
                    result.primitives.push_back(ReadPrimitive(node.mesh->primitives[i], *data, transform, total));
                }
            }
            for (std::size_t i = 0; i < node.children_count; ++i) recurse(recurse, *node.children[i]);
        };
        for (std::size_t i = 0; i < scene.nodes_count; ++i) visit(visit, *scene.nodes[i]);
        if (result.primitives.empty()) throw std::runtime_error("The default scene has no triangle mesh.");
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

}  // namespace tinyworld
