#include "Engine/Scene/SceneMesh.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>

namespace {

using tinyworld::SceneVertex;

//————————————————————————————————————————————————————————————————————————————————
// 打开课程原始 GLB，抽取位置、法线和线性底色。
// 第二章的三个原件均是单一未变换、无索引的静态网格。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> ReadModel(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open model: " + path.string());
    }
    const std::vector<char> bytes(std::istreambuf_iterator<char>(input), {});
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &raw) != cgltf_result_success) {
        throw std::runtime_error("Cannot parse GLB: " + path.string());
    }
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> model(raw, cgltf_free);
    if (model->file_type != cgltf_file_type_glb || model->buffers_count != 1 ||
        model->buffers[0].uri || cgltf_load_buffers(&options, model.get(), nullptr) != cgltf_result_success ||
        cgltf_validate(model.get()) != cgltf_result_success || model->meshes_count != 1 ||
        model->nodes_count != 1 || model->nodes[0].mesh != model->meshes ||
        model->nodes[0].has_matrix || model->nodes[0].has_translation ||
        model->nodes[0].has_rotation || model->nodes[0].has_scale) {
        throw std::runtime_error("Expected one embedded, untransformed chapter 02 mesh: " + path.string());
    }

    std::vector<SceneVertex> vertices;
    const auto& mesh = model->meshes[0];
    for (std::size_t part = 0; part < mesh.primitives_count; ++part) {
        const auto& primitive = mesh.primitives[part];
        const auto* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
        const auto* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
        if (primitive.type != cgltf_primitive_type_triangles || primitive.indices ||
            !positions || !normals || positions->type != cgltf_type_vec3 || normals->type != cgltf_type_vec3 ||
            positions->count != normals->count || positions->count % 3 != 0 ||
            positions->count > tinyworld::kMaxMeshVertices - vertices.size() ||
            !primitive.material || !primitive.material->has_pbr_metallic_roughness) {
            throw std::runtime_error("Unexpected chapter 02 primitive: " + path.string());
        }
        // 把每个几何片段的材质底色复制给该片段的顶点，运行时一次绘制一件物体。
        const auto& color = primitive.material->pbr_metallic_roughness.base_color_factor;
        for (std::size_t i = 0; i < positions->count; ++i) {
            SceneVertex vertex{};
            if (!cgltf_accessor_read_float(positions, i, vertex.position.data(), 3) ||
                !cgltf_accessor_read_float(normals, i, vertex.normal.data(), 3)) {
                throw std::runtime_error("Cannot read chapter 02 vertex: " + path.string());
            }
            std::copy_n(color, 3, vertex.color.begin());
            const auto finite = [](float value) { return std::isfinite(value); };
            if (!std::all_of(vertex.position.begin(), vertex.position.end(), finite) ||
                !std::all_of(vertex.normal.begin(), vertex.normal.end(), finite) ||
                !std::all_of(vertex.color.begin(), vertex.color.end(), finite)) {
                throw std::runtime_error("Non-finite chapter 02 vertex: " + path.string());
            }
            vertices.push_back(vertex);
        }
    }
    if (vertices.empty()) {
        throw std::runtime_error("Chapter 02 mesh contains no vertices: " + path.string());
    }
    return vertices;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 从一份课程 GLB 构建 TWM2 顶点包。失败时以非零状态退出构建。
//————————————————————————————————————————————————————————————————————————————————
int main(int argc, char** argv) {
    static_assert(std::endian::native == std::endian::little);
    if (argc != 3) {
        std::cerr << "Usage: TinyWorldMeshPacker input.glb output.twmesh\n";
        return 1;
    }
    try {
        const auto vertices = ReadModel(argv[1]);
        const auto count = static_cast<std::uint32_t>(vertices.size());
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        output.write(tinyworld::kMeshMagic.data(), tinyworld::kMeshMagic.size());
        output.write(reinterpret_cast<const char*>(&count), sizeof(count));
        output.write(reinterpret_cast<const char*>(vertices.data()), count * sizeof(SceneVertex));
        output.close();
        if (!output) {
            throw std::runtime_error("Cannot write packed vertex data");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
