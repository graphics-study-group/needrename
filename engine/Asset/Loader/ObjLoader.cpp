#include "ObjLoader.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Mesh/MeshAsset.h>
#include <Asset/Scene/SceneAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <Framework/component/RenderComponent/StaticMeshComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <tiny_obj_loader.h>

#include <unordered_map>

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
            for (const auto &[id, new_shape] : material_id_map) {
                shapes.push_back(new_shape);
                if (id < 0) continue;
                materials.push_back(origin_materials[id]);
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
        std::unordered_map<std::string, size_t> material_name_to_index;

        for (const auto &material : materials) {
            std::shared_ptr<MaterialAsset> m_material_asset = std::make_shared<MaterialAsset>();
            auto it = material_name_to_index.find(material.name);
            if (it == material_name_to_index.end()) {
                material_name_to_index[material.name] = m_material_assets.size();
                LoadMaterialAssetFromTinyObj(*m_material_asset, material, path.parent_path());
                m_material_assets.push_back(m_material_asset);
                for (const auto &[name, property] : m_material_asset->m_properties) {
                    if (property.m_type == MaterialProperty::Type::Texture) {
                        auto ref = std::any_cast<AssetRef>(property.m_value);
                        m_texture_assets.push_back(ref.as<Image2DTextureAsset>());
                    }
                }
            }
        }

        Serialization::Archive archive;
        auto database = m_database.lock();
        archive.prepare_save();
        m_mesh_asset->save_asset_to_archive(archive);
        database->SaveArchive(archive, AssetPath(*database, path_in_project / (m_mesh_asset->m_name + ".asset")));
        for (const auto &material : m_material_assets) {
            archive.clear();
            archive.prepare_save();
            material->save_asset_to_archive(archive);
            database->SaveArchive(archive, AssetPath(*database, path_in_project / (material->m_name + ".asset")));
        }
        for (const auto &texture : m_texture_assets) {
            archive.clear();
            archive.prepare_save();
            texture->save_asset_to_archive(archive);
            database->SaveArchive(archive, AssetPath(*database, path_in_project / (texture->m_name + ".asset")));
        }

        auto &temp_scene = MainClass::GetInstance()->GetWorldSystem()->CreateScene();
        auto &go = temp_scene.CreateGameObject();
        go.m_name = m_mesh_asset->m_name;
        auto &mesh_component = go.AddComponent<StaticMeshComponent>();
        mesh_component.m_mesh_asset = AssetRef(std::dynamic_pointer_cast<Asset>(m_mesh_asset));
        auto submesh_count = m_mesh_asset->m_submeshes.size();
        for (size_t i = 0; i < submesh_count; i++) {
            if (i < materials.size()) {
                mesh_component.m_material_assets.push_back(
                    AssetRef(std::dynamic_pointer_cast<Asset>(m_material_assets[material_name_to_index[materials[i].name]]))
                );
            } else {
                mesh_component.m_material_assets.push_back(database->GetNewAssetRef(
                    AssetPath(*database, std::filesystem::path("~/materials/solid_color_dark_grey.asset"))
                ));
            }
        }

        // auto scene_asset = std::make_unique<SceneAsset>(std::move(temp_scene));
        // archive.clear();
        // archive.prepare_save();
        // scene_asset->save_asset_to_archive(archive);
        // database->SaveArchive(archive, AssetPath(*database, path_in_project / ("GO_" + m_mesh_asset->m_name + ".asset")));
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

            std::vector <float> position, color, normal, texcoord0;

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
                position.size() * sizeof(float) +
                color.size() * sizeof(float) +
                normal.size() * sizeof(float) +
                texcoord0.size() * sizeof(float)
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

    void ObjLoader::LoadMaterialAssetFromTinyObj(
        MaterialAsset &material_asset, const tinyobj::material_t &material, const std::filesystem::path &base_path
    ) {
        auto database = m_database.lock();

        material_asset.m_name = material.name;

        switch (material.illum) {
        case 2: // Blinn-Phong
        {
            material_asset.m_library =
                database->GetNewAssetRef(AssetPath(*database, std::filesystem::path("~/material_libraries/BlinnPhongLibrary.asset")));
            material_asset.m_properties["ambient_color"] =
                glm::vec4{material.ambient[0], material.ambient[1], material.ambient[2], 1.0f};
            material_asset.m_properties["specular_color"] =
                glm::vec4{material.specular[0], material.specular[1], material.specular[2], (float)material.shininess};
            if (!material.diffuse_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.diffuse_texname);
                texture->m_name += "_tex_diffuse";
                material_asset.m_properties["base_tex"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            break;
        }
        default: // unknown model, load every property
        {
            material_asset.m_library =
                database->GetNewAssetRef(AssetPath(*database, std::filesystem::path("~/material_libraries/BlinnPhongLibrary.asset")));
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
                texture->m_name += "_tex_ambient";
                material_asset.m_properties["ambient_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.diffuse_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.diffuse_texname);
                texture->m_name += "_tex_diffuse";
                material_asset.m_properties["diffuse_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.specular_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.specular_texname);
                texture->m_name += "_tex_specular";
                material_asset.m_properties["specular_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.specular_highlight_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.specular_highlight_texname);
                texture->m_name += "_tex_specular_highlight";
                material_asset.m_properties["specular_highlight_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.bump_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.bump_texname);
                texture->m_name += "_tex_bump";
                material_asset.m_properties["bump_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.displacement_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.displacement_texname);
                texture->m_name += "_tex_displacement";
                material_asset.m_properties["displacement_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.alpha_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.alpha_texname);
                texture->m_name += "_tex_alpha";
                material_asset.m_properties["alpha_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.roughness_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.roughness_texname);
                texture->m_name += "_tex_roughness";
                material_asset.m_properties["roughness_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.metallic_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.metallic_texname);
                texture->m_name += "_tex_metallic";
                material_asset.m_properties["metallic_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.sheen_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.sheen_texname);
                texture->m_name += "_tex_sheen";
                material_asset.m_properties["sheen_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.emissive_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.emissive_texname);
                texture->m_name += "_tex_emissive";
                material_asset.m_properties["emissive_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            if (!material.normal_texname.empty()) {
                auto texture = std::make_shared<Image2DTextureAsset>();
                texture->LoadFromFile(base_path / material.normal_texname);
                texture->m_name += "_tex_normal";
                material_asset.m_properties["normal_texture"] =
                    {AssetRef(std::dynamic_pointer_cast<Asset>(texture)), MaterialProperty::Type::Texture};
            }
            break;
        }
        }
    }
} // namespace Engine
