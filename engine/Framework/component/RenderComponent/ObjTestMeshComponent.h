#ifndef COMPONENT_RENDERCOMPONENT_OBJTESTMESHCOMPONENT_INCLUDED
#define COMPONENT_RENDERCOMPONENT_OBJTESTMESHCOMPONENT_INCLUDED

#include "Asset/AssetManager/AssetManager.h"
#include "Framework/component/RenderComponent/StaticMeshComponent.h"
#include "MainClass.h"
#include <tiny_obj_loader.h>

namespace Engine {
    /**
     * @brief A mesh component that loads an obj file on construct.
     *
     * @warning For test only! Never use this component in production.
     * Always include this header after all your inclusion to avoid definition problems.
     */
    class ObjTestMeshComponent : public StaticMeshComponent {
    public:
        static void LoadMeshAssetFromTinyObj(
            MeshAsset &mesh_asset, const tinyobj::attrib_t &attrib, const std::vector<tinyobj::shape_t> &shapes
        ) {
            mesh_asset.m_submeshes.clear();

            const auto &positions = attrib.vertices;
            const auto &normals = attrib.normals;
            const auto &uvs = attrib.texcoords;
            const auto &colors = attrib.colors;

            for (const auto &shape : shapes) {
                mesh_asset.m_submeshes.emplace_back();
                auto &submesh = mesh_asset.m_submeshes.back();
                uint32_t vertex_id = 0;
                std::map<std::tuple<int, int, int>, uint32_t> vertex_id_map;

                std::vector<float> position, color, normal, texcoord0;

                for (const auto &index : shape.mesh.indices) {
                    std::tuple<int, int, int> key(index.vertex_index, index.normal_index, index.texcoord_index);
                    if (vertex_id_map.find(key) == vertex_id_map.end()) {
                        vertex_id_map[key] = vertex_id++;
                        submesh.positions.type = VertexAttributeType::SFloat32x3;
                        position.push_back(positions[index.vertex_index * 3]);
                        position.push_back(positions[index.vertex_index * 3 + 1]);
                        position.push_back(positions[index.vertex_index * 3 + 2]);

                        if (colors.size() > 0) {
                            submesh.color.type = VertexAttributeType::SFloat32x3;
                            color.push_back(colors[index.vertex_index * 3]);
                            color.push_back(colors[index.vertex_index * 3 + 1]);
                            color.push_back(colors[index.vertex_index * 3 + 2]);
                        }
                        if (index.normal_index >= 0) {
                            submesh.normal.type = VertexAttributeType::SFloat32x3;
                            normal.push_back(normals[index.normal_index * 3]);
                            normal.push_back(normals[index.normal_index * 3 + 1]);
                            normal.push_back(normals[index.normal_index * 3 + 2]);
                        }
                        if (index.texcoord_index >= 0) {
                            submesh.texcoord0.type = VertexAttributeType::SFloat32x2;
                            texcoord0.push_back(uvs[index.texcoord_index * 2]);
                            texcoord0.push_back(uvs[index.texcoord_index * 2 + 1]);
                        }
                    }
                    submesh.m_indices.push_back(vertex_id_map[key]);
                }
                submesh.vertex_count = vertex_id;
                submesh.m_vertex_attributes.reserve(
                    position.size() * sizeof(float) + color.size() * sizeof(float) + normal.size() * sizeof(float)
                    + texcoord0.size() * sizeof(float)
                );

                submesh.positions.buffer_offset = submesh.m_vertex_attributes.size();
                submesh.m_vertex_attributes.insert(
                    submesh.m_vertex_attributes.end(),
                    reinterpret_cast<const std::byte *>(position.data()),
                    reinterpret_cast<const std::byte *>(position.data() + position.size())
                );
                submesh.positions.buffer_size = position.size() * sizeof(float);

                submesh.color.buffer_offset = submesh.m_vertex_attributes.size();
                submesh.m_vertex_attributes.insert(
                    submesh.m_vertex_attributes.end(),
                    reinterpret_cast<const std::byte *>(color.data()),
                    reinterpret_cast<const std::byte *>(color.data() + color.size())
                );
                submesh.color.buffer_size = color.size() * sizeof(float);

                submesh.normal.buffer_offset = submesh.m_vertex_attributes.size();
                submesh.m_vertex_attributes.insert(
                    submesh.m_vertex_attributes.end(),
                    reinterpret_cast<const std::byte *>(normal.data()),
                    reinterpret_cast<const std::byte *>(normal.data() + normal.size())
                );
                submesh.normal.buffer_size = normal.size() * sizeof(float);

                submesh.texcoord0.buffer_offset = submesh.m_vertex_attributes.size();
                submesh.m_vertex_attributes.insert(
                    submesh.m_vertex_attributes.end(),
                    reinterpret_cast<const std::byte *>(texcoord0.data()),
                    reinterpret_cast<const std::byte *>(texcoord0.data() + texcoord0.size())
                );
                submesh.texcoord0.buffer_size = texcoord0.size() * sizeof(float);
            }
        }

        void LoadMesh(std::filesystem::path mesh) {
            tinyobj::ObjReaderConfig reader_config{};
            tinyobj::ObjReader reader{};

            if (!reader.ParseFromFile(mesh.string(), reader_config)) {
                SDL_LogCritical(0, "Failed to load OBJ file %s", mesh.string().c_str());
                if (!reader.Error().empty()) {
                    SDL_LogCritical(0, "TinyObjLoader reports: %s", reader.Error().c_str());
                }
                throw std::runtime_error("Cannot load OBJ file");
            }

            SDL_LogInfo(0, "Loaded OBJ file %s", mesh.string().c_str());
            if (!reader.Warning().empty()) {
                SDL_LogWarn(0, "TinyObjLoader reports: %s", reader.Warning().c_str());
            }

            const auto &attrib = reader.GetAttrib();
            const auto &origin_shapes = reader.GetShapes();
            std::vector<tinyobj::shape_t> shapes;
            // const auto &origin_materials = reader.GetMaterials();

            // We dont need materials from the obj file.
            std::vector<tinyobj::material_t> materials;

            // Split the subshapes by material
            for (size_t shp = 0; shp < origin_shapes.size(); shp++) {
                const auto &shape = origin_shapes[shp];
                auto shape_vertices_size = shape.mesh.num_face_vertices.size();
                std::map<int, tinyobj::shape_t> material_id_map;
                int shape_id = 0;
                for (size_t fc = 0; fc < shape_vertices_size; fc++) {
                    auto &material_id = shape.mesh.material_ids[fc];
                    if (material_id_map.find(material_id) == material_id_map.end()) {
                        material_id_map[material_id] = tinyobj::shape_t{
                            .name = shape.name + "_" + std::to_string(shape_id++),
                            .mesh = tinyobj::mesh_t{},
                            .lines = tinyobj::lines_t{},
                            .points = tinyobj::points_t{}
                        };
                    }
                    auto &new_shape = material_id_map[material_id];
                    unsigned int face_vertex_count = shape.mesh.num_face_vertices[fc];
                    assert(face_vertex_count == 3);
                    new_shape.mesh.num_face_vertices.push_back(face_vertex_count);
                    new_shape.mesh.material_ids.push_back(material_id);
                    new_shape.mesh.smoothing_group_ids.push_back(shape.mesh.smoothing_group_ids[fc]);
                    for (unsigned int vrtx = 0; vrtx < face_vertex_count; vrtx++) {
                        new_shape.mesh.indices.push_back(shape.mesh.indices[fc * 3 + vrtx]);
                    }
                }

                for (const auto &[_, new_shape] : material_id_map) {
                    shapes.push_back(new_shape);
                    materials.push_back(tinyobj::material_t{});
                    std::fill(
                        shapes.back().mesh.material_ids.begin(),
                        shapes.back().mesh.material_ids.end(),
                        materials.size() - 1
                    );
                }
            }

            auto am = MainClass::GetInstance()->GetAssetManager();
            this->m_mesh_asset = AssetRef(am->CreateAsset<MeshAsset>());
            LoadMeshAssetFromTinyObj(*(this->m_mesh_asset.as<MeshAsset>()), attrib, shapes);

            if (this->m_mesh_asset.as<MeshAsset>()->m_submeshes.empty()) {
                throw std::runtime_error("No valid submesh is generated from file.");
            }
        }

    public:
        ObjTestMeshComponent(const GameObject &parent) : StaticMeshComponent(parent) {
        }

        ~ObjTestMeshComponent() = default;
    };
} // namespace Engine

#endif // COMPONENT_RENDERCOMPONENT_OBJTESTMESHCOMPONENT_INCLUDED
