#include "ObjLoader.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Mesh/MeshAsset.h>
#include <Asset/Scene/GameObjectAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <Framework/component/RenderComponent/MeshComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <tiny_obj_loader.h>

namespace Engine {
    ObjLoader::ObjLoader() {
        m_database = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
        assert(!m_database.expired());
    }

    void ObjLoader::LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project) {
        tinyobj::ObjReaderConfig reader_config{};
        tinyobj::ObjReader reader{};

        if (!reader.ParseFromFile(path.string(), reader_config))
            throw std::runtime_error("Cannot load OBJ file:" + path.string() + ", " + reader.Error());

        SDL_LogInfo(0, "Loaded OBJ file %s", path.string().c_str());
        if (!reader.Warning().empty()) SDL_LogWarn(0, "TinyObjLoader reports: %s", reader.Warning().c_str());

        const auto &attrib = reader.GetAttrib();
        const auto &origin_shapes = reader.GetShapes();
        std::vector<tinyobj::shape_t> shapes;
        const auto &origin_materials = reader.GetMaterials();
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
                materials.push_back(origin_materials[new_shape.mesh.material_ids[0]]);
                std::fill(
                    shapes.back().mesh.material_ids.begin(), shapes.back().mesh.material_ids.end(), materials.size() - 1
                );
            }
        }

        std::shared_ptr<MeshAsset> m_mesh_asset = std::make_shared<MeshAsset>();
        m_mesh_asset->m_name = path.stem().string();
        LoadMeshAssetFromTinyObj(*m_mesh_asset, attrib, shapes);

        std::vector<std::shared_ptr<MaterialAsset>> m_material_assets;
        std::vector<std::shared_ptr<Image2DTextureAsset>> m_texture_assets;

        for (const auto &material : materials) {
            std::shared_ptr<MaterialAsset> m_material_asset = std::make_shared<MaterialAsset>();
            LoadMaterialAssetFromTinyObj(*m_material_asset, material, path.parent_path());
            m_material_assets.push_back(m_material_asset);

            for (const auto &[name, property] : m_material_asset->m_properties) {
                if (property.m_type == MaterialProperty::Type::Texture) {
                    auto ref = std::any_cast<std::shared_ptr<AssetRef>>(property.m_value);
                    m_texture_assets.push_back(ref->as<Image2DTextureAsset>());
                }
            }
        }

        Serialization::Archive archive;
        auto database = m_database.lock();
        archive.prepare_save();
        m_mesh_asset->save_asset_to_archive(archive);
        database->SaveArchive(archive, path_in_project / (m_mesh_asset->m_name + ".asset"));
        for (const auto &material : m_material_assets) {
            archive.clear();
            archive.prepare_save();
            material->save_asset_to_archive(archive);
            database->SaveArchive(archive, path_in_project / (material->m_name + ".asset"));
        }
        for (const auto &texture : m_texture_assets) {
            archive.clear();
            archive.prepare_save();
            texture->save_asset_to_archive(archive);
            database->SaveArchive(archive, path_in_project / (texture->m_name + ".asset"));
        }

        std::shared_ptr<GameObjectAsset> m_game_object_asset = std::make_shared<GameObjectAsset>();
        m_game_object_asset->m_MainObject = MainClass::GetInstance()->GetWorldSystem()->CreateGameObject<GameObject>();
        m_game_object_asset->m_MainObject->m_name = m_mesh_asset->m_name;
        std::shared_ptr<MeshComponent> m_mesh_component =
            std::make_shared<MeshComponent>(m_game_object_asset->m_MainObject);
        m_mesh_component->m_mesh_asset = std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(m_mesh_asset));
        for (const auto &material : m_material_assets) {
            m_mesh_component->m_material_assets.push_back(
                std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(material))
            );
        }
        m_game_object_asset->m_MainObject->AddComponent(m_mesh_component);

        archive.clear();
        archive.prepare_save();
        m_game_object_asset->save_asset_to_archive(archive);
        database->SaveArchive(archive, path_in_project / ("GO_" + m_mesh_asset->m_name + ".asset"));
    }

    void ObjLoader::LoadMeshAssetFromTinyObj(
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

            for (const auto &index : shape.mesh.indices) {
                std::tuple<int, int, int> key(index.vertex_index, index.normal_index, index.texcoord_index);
                if (vertex_id_map.find(key) == vertex_id_map.end()) {
                    vertex_id_map[key] = vertex_id++;
                    submesh.positions.type = MeshAsset::Submesh::Attributes::AttributeType::Floatx3;
                    submesh.positions.attribf.push_back(positions[index.vertex_index * 3]);
                    submesh.positions.attribf.push_back(positions[index.vertex_index * 3 + 1]);
                    submesh.positions.attribf.push_back(positions[index.vertex_index * 3 + 2]);


                    if (colors.size() > 0) {
                        submesh.color.type = MeshAsset::Submesh::Attributes::AttributeType::Floatx3;
                        submesh.color.attribf.push_back(colors[index.vertex_index * 3]);
                        submesh.color.attribf.push_back(colors[index.vertex_index * 3 + 1]);
                        submesh.color.attribf.push_back(colors[index.vertex_index * 3 + 2]);
                    }
                    if (index.normal_index >= 0) {
                        submesh.normal.type = MeshAsset::Submesh::Attributes::AttributeType::Floatx3;
                        submesh.normal.attribf.push_back(normals[index.normal_index * 3]);
                        submesh.normal.attribf.push_back(normals[index.normal_index * 3 + 1]);
                        submesh.normal.attribf.push_back(normals[index.normal_index * 3 + 2]);
                    }
                    if (index.texcoord_index >= 0) {
                        submesh.texcoord0.type = MeshAsset::Submesh::Attributes::AttributeType::Floatx2;
                        submesh.texcoord0.attribf.push_back(uvs[index.texcoord_index * 2]);
                        submesh.texcoord0.attribf.push_back(uvs[index.texcoord_index * 2 + 1]);
                    }
                }
                submesh.m_indices.push_back(vertex_id_map[key]);
            }

            submesh.vertex_count = vertex_id;
        }
    }

    void ObjLoader::LoadMaterialAssetFromTinyObj(
        MaterialAsset &material_asset, const tinyobj::material_t &material, const std::filesystem::path &base_path
    ) {
        material_asset.m_name = material.name;

        switch (material.illum) {
        case 2: // Blinn-Phong
        {
            material_asset.m_library =
                m_database.lock()->GetNewAssetRef(std::filesystem::path("~/material_libraries/BlinnPhongLibrary.asset"));
            material_asset.m_properties["ambient_color"] =
                glm::vec4{material.ambient[0], material.ambient[1], material.ambient[2], 1.0f};
            material_asset.m_properties["specular_color"] =
                glm::vec4{material.specular[0], material.specular[1], material.specular[2], (float)material.shininess};
            if (!material.diffuse_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.diffuse_texname);
                material_asset.m_properties["base_tex"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            break;
        }
        default: // unknown model, load every property
        {
            material_asset.m_library =
                m_database.lock()->GetNewAssetRef(std::filesystem::path("~/material_libraries/BlinnPhongLibrary.asset"));
            material_asset.m_properties["ambient"] =
                glm::vec4{material.ambient[0], material.ambient[1], material.ambient[2], 1.0f};
            material_asset.m_properties["diffuse"] =
                glm::vec4{material.diffuse[0], material.diffuse[1], material.diffuse[2], 1.0f};
            material_asset.m_properties["specular"] =
                glm::vec4{material.specular[0], material.specular[1], material.specular[2], 1.0f};
            material_asset.m_properties["transmittance"] =
                glm::vec4{material.transmittance[0], material.transmittance[1], material.transmittance[2], 1.0f};
            material_asset.m_properties["emission"] =
                glm::vec4{material.emission[0], material.emission[1], material.emission[2], 1.0f};
            material_asset.m_properties["shininess"] = (float)material.shininess;
            material_asset.m_properties["ior"] = (float)material.ior;
            material_asset.m_properties["dissolve"] = (float)material.dissolve;
            if (!material.ambient_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.ambient_texname);
                material_asset.m_properties["ambient_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.diffuse_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.diffuse_texname);
                material_asset.m_properties["diffuse_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.specular_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.specular_texname);
                material_asset.m_properties["specular_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.specular_highlight_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.specular_highlight_texname);
                material_asset.m_properties["specular_highlight_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.bump_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.bump_texname);
                material_asset.m_properties["bump_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.displacement_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.displacement_texname);
                material_asset.m_properties["displacement_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.alpha_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.alpha_texname);
                material_asset.m_properties["alpha_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.roughness_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.roughness_texname);
                material_asset.m_properties["roughness_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.metallic_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.metallic_texname);
                material_asset.m_properties["metallic_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.sheen_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.sheen_texname);
                material_asset.m_properties["sheen_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.emissive_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.emissive_texname);
                material_asset.m_properties["emissive_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            if (!material.normal_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.normal_texname);
                material_asset.m_properties["normal_texture"] =
                    std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(texture));
            }
            break;
        }
        }
    }
} // namespace Engine
