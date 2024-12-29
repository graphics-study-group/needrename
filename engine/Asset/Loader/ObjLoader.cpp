#include "ObjLoader.h"
#include <tiny_obj_loader.h>
#include <nlohmann/json.hpp>
#include <Asset/Mesh/MeshAsset.h>
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <Asset/Scene/GameObjectAsset.h>
#include <Framework/object/GameObject.h>
#include <Framework/component/RenderComponent/MeshComponent.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>

namespace Engine
{
    ObjLoader::ObjLoader()
    {
        m_manager = MainClass::GetInstance()->GetAssetManager();
    }

    void ObjLoader::LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project)
    {
        tinyobj::ObjReaderConfig reader_config{};
        tinyobj::ObjReader reader{};

        if (!reader.ParseFromFile(path.string(), reader_config))
            throw std::runtime_error("Cannot load OBJ file:" + path.string());

        SDL_LogInfo(0, "Loaded OBJ file %s", path.string().c_str());
        if (!reader.Warning().empty())
            SDL_LogWarn(0, "TinyObjLoader reports: %s", reader.Warning().c_str());

        const auto &attrib = reader.GetAttrib();
        const auto &origin_shapes = reader.GetShapes();
        std::vector<tinyobj::shape_t> shapes;
        const auto &origin_materials = reader.GetMaterials();
        std::vector<tinyobj::material_t> materials;

        // Split the subshapes by material
        for (size_t shp = 0; shp < origin_shapes.size(); shp++)
        {
            const auto &shape = origin_shapes[shp];
            auto shape_vertices_size = shape.mesh.num_face_vertices.size();
            std::map<int, tinyobj::shape_t> material_id_map;
            int shape_id = 0;
            for (size_t fc = 0; fc < shape_vertices_size; fc++)
            {
                auto &material_id = shape.mesh.material_ids[fc];
                if (material_id_map.find(material_id) == material_id_map.end())
                {
                    material_id_map[material_id] = tinyobj::shape_t{
                        .name = shape.name + "_" + std::to_string(shape_id++),
                        .mesh = tinyobj::mesh_t{},
                        .lines = tinyobj::lines_t{},
                        .points = tinyobj::points_t{}};
                }
                auto &new_shape = material_id_map[material_id];
                unsigned int face_vertex_count = shape.mesh.num_face_vertices[fc];
                assert(face_vertex_count == 3);
                new_shape.mesh.num_face_vertices.push_back(face_vertex_count);
                new_shape.mesh.material_ids.push_back(material_id);
                new_shape.mesh.smoothing_group_ids.push_back(shape.mesh.smoothing_group_ids[fc]);
                for (unsigned int vrtx = 0; vrtx < face_vertex_count; vrtx++)
                {
                    new_shape.mesh.indices.push_back(shape.mesh.indices[fc * 3 + vrtx]);
                }
            }
            for (const auto &[_, new_shape] : material_id_map)
            {
                shapes.push_back(new_shape);
                materials.push_back(origin_materials[new_shape.mesh.material_ids[0]]);
                std::fill(
                    shapes.back().mesh.material_ids.begin(),
                    shapes.back().mesh.material_ids.end(),
                    materials.size() - 1);
            }
        }

        std::shared_ptr<MeshAsset> m_mesh_asset = std::make_shared<MeshAsset>();
        m_mesh_asset->m_name = path.stem().string();
        m_mesh_asset->LoadFromTinyobj(attrib, shapes); // TODO: Move this to ObjLoader class

        std::vector<std::shared_ptr<MaterialAsset>> m_material_assets;
        std::vector<std::shared_ptr<Image2DTextureAsset>> m_texture_assets;

        for (const auto &material : materials)
        {
            std::shared_ptr<MaterialAsset> m_material_asset = std::make_shared<MaterialAsset>();
            m_material_asset->LoadFromTinyObj(material, path.parent_path()); // TODO: Move this to ObjLoader class
            m_material_assets.push_back(m_material_asset);
            for (const auto &[name, texture] : m_material_asset->m_textures)
            {
                m_texture_assets.push_back(std::dynamic_pointer_cast<Image2DTextureAsset>(texture));
            }
        }

        Serialization::Archive archive;
        archive.prepare_save();
        m_mesh_asset->save_asset_to_archive(archive);
        archive.save_to_file(m_manager.lock()->GetAssetsDirectory() / path_in_project / (m_mesh_asset->m_name + ".mesh.asset"));
        m_manager.lock()->AddAsset(m_mesh_asset->GetGUID(), path_in_project / (m_mesh_asset->m_name + ".mesh.asset"));
        for (const auto &material : m_material_assets)
        {
            archive.clear();
            archive.prepare_save();
            material->save_asset_to_archive(archive);
            archive.save_to_file(m_manager.lock()->GetAssetsDirectory() / path_in_project / (material->m_name + ".material.asset"));
            m_manager.lock()->AddAsset(material->GetGUID(), path_in_project / (material->m_name + ".material.asset"));
        }
        for (const auto &texture : m_texture_assets)
        {
            archive.clear();
            archive.prepare_save();
            texture->save_asset_to_archive(archive);
            archive.save_to_file(m_manager.lock()->GetAssetsDirectory() / path_in_project / (texture->m_name + ".png.asset"));
            m_manager.lock()->AddAsset(texture->GetGUID(), path_in_project / (texture->m_name + ".png.asset"));
        }

        std::shared_ptr<GameObjectAsset> m_game_object_asset = std::make_shared<GameObjectAsset>();
        m_game_object_asset->m_MainObject = MainClass::GetInstance()->GetWorldSystem()->CreateGameObject<GameObject>();
        std::shared_ptr<MeshComponent> m_mesh_component = std::make_shared<MeshComponent>(m_game_object_asset->m_MainObject);
        m_mesh_component->m_mesh_asset = m_mesh_asset;
        m_mesh_component->m_material_assets = m_material_assets;
        m_game_object_asset->m_MainObject->AddComponent(m_mesh_component);

        archive.clear();
        archive.prepare_save();
        m_game_object_asset->save_asset_to_archive(archive);
        archive.save_to_file(m_manager.lock()->GetAssetsDirectory() / path_in_project / (m_mesh_asset->m_name + ".gameobject.asset"));
        m_manager.lock()->AddAsset(m_game_object_asset->GetGUID(), path_in_project / (m_mesh_asset->m_name + ".gameobject.asset"));
    }
}
