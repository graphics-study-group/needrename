#ifndef COMPONENT_RENDERCOMPONENT_OBJTESTMESHCOMPONENT_INCLUDED
#define COMPONENT_RENDERCOMPONENT_OBJTESTMESHCOMPONENT_INCLUDED

#include <tiny_obj_loader.h>
#include <SDL3/SDL.h>
#include "Asset/Loader/ObjLoader.h"
#include "Framework/component/RenderComponent/MeshComponent.h"

namespace Engine {
    /**
     * @brief A mesh component that loads an obj file on construct.
     * 
     * @warning For test only! Never use this component in production.
     * Always include this header after all your inclusion to avoid definition problems.
     */
    class ObjTestMeshComponent : public MeshComponent {

        void LoadMesh(std::filesystem::path mesh) {
            tinyobj::ObjReaderConfig reader_config{};
            tinyobj::ObjReader reader{};

            m_materials.clear();
            m_submeshes.clear();

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
            const auto &origin_materials = reader.GetMaterials();

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
                        shapes.back().mesh.material_ids.begin(), shapes.back().mesh.material_ids.end(), materials.size() - 1
                    );
                }
            }

            this->m_mesh_asset =
                std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(std::make_shared<MeshAsset>()));
            ObjLoader loader;
            loader.LoadMeshAssetFromTinyObj(*(this->m_mesh_asset->as<MeshAsset>()), attrib, shapes);

            assert(m_mesh_asset && m_mesh_asset->IsValid());
            m_submeshes.clear();
            size_t submesh_count = m_mesh_asset->as<MeshAsset>()->GetSubmeshCount();
            for (size_t i = 0; i < submesh_count; i++) {
                m_submeshes.push_back(std::make_shared<HomogeneousMesh>(m_system.lock()->GetAllocatorState(), m_mesh_asset, i));
            }
        }

    public:
        ObjTestMeshComponent(
            std::filesystem::path mesh_file_name,
            ObjectHandle go = 0u
        ) : MeshComponent(go) {
            LoadMesh(mesh_file_name);
        }

        ~ObjTestMeshComponent() {
            m_materials.clear();
            m_submeshes.clear();
        }

        virtual void RenderInit() override {
            assert(!"This component has no mesh nor material asset to load from.");
        }
    };
}

#endif // COMPONENT_RENDERCOMPONENT_OBJTESTMESHCOMPONENT_INCLUDED
