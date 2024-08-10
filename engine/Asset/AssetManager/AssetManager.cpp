#include "AssetManager.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <cassert>
#include <iostream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>

#include "Asset/Mesh/Mesh.h"
#include "Render/Material/Shadeless.h"

namespace Engine
{
    std::mt19937_64 AssetManager::m_guid_gen{std::random_device{}()};

    void AssetManager::LoadProject(std::filesystem::path path)
    {
        if(!std::filesystem::exists(path))
            throw std::runtime_error("Project path does not exist");
        m_projectPath = path;
        auto asset_path = path / "assets";
        if (!std::filesystem::exists(asset_path))
            std::filesystem::create_directory(asset_path);
        for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(asset_path))
        {
            std::filesystem::path relative_path = std::filesystem::relative(entry.path(), asset_path);
            if (relative_path.extension() == ".asset")
            {
                std::ifstream file(entry.path());
                if (file.is_open())
                {
                    nlohmann::json json_data = nlohmann::json::parse(file);
                    GUID guid = stringToGUID(json_data["guid"]);
                    AddAsset(guid, relative_path.parent_path() / relative_path.stem());
                    file.close();
                }
                else
                    throw std::runtime_error("Failed to open asset file");
            }
        }
    }

    // TODO: load asset not implemented
    void AssetManager::LoadExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project)
    {
        if (!std::filesystem::exists(GetAssetsDirectory() / path_in_project))
            std::filesystem::create_directory(GetAssetsDirectory() / path_in_project);
        std::string extension = resourcePath.extension().string();
        if (extension == ".obj")
        {
            LoadObjResource(resourcePath, path_in_project);
        }
        else
        {
            throw std::runtime_error("Unsupported file format");
        }
    }

    template <typename AssetType>
    void AssetManager::SaveAsset(AssetType asset, std::filesystem::path path)
    {
        throw std::runtime_error("Not implemented");
    }

    std::filesystem::path AssetManager::GetAssetPath(GUID guid) const
    {
        auto it = m_assets.find(guid);
        if (it != m_assets.end())
            return GetAssetsDirectory() / it->second;
        else
            throw std::runtime_error("Asset not found");
    }

    std::filesystem::path AssetManager::GetAssetPath(const std::shared_ptr<Asset> &asset) const
    {
        return GetAssetPath(asset->GetGUID());
    }

    void AssetManager::AddAsset(const GUID &guid, const std::filesystem::path &path)
    {
        if(m_assets.find(guid) != m_assets.end())
            throw std::runtime_error("asset GUID already exists");
        m_assets[guid] = path;
    }

    void AssetManager::LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project)
    {
        tinyobj::ObjReader reader;
        reader.ParseFromFile(path.string());
        if (!reader.Error().empty())
        {
            throw std::runtime_error("TinyObjReader reports errors reading " + path.string());
        }

        if (!reader.Warning().empty())
        {
            std::cerr << "TinyObjReader reports warnings reading " << path.string() << std::endl;
            std::cerr << "\t" << reader.Warning() << std::endl;
        }

        const tinyobj::attrib_t &attrib = reader.GetAttrib();
        const std::vector<tinyobj::shape_t> &shapes = reader.GetShapes();
        const std::vector<tinyobj::material_t> &materials = reader.GetMaterials();

        GUID mesh_guid = GenerateGUID();
        nlohmann::json mesh_json;
        std::vector<GUID> material_guids;
        Mesh assetMesh;

        std::unordered_map<std::string, GUID> texture_path_guid_map;
        for (size_t i = 0; i < materials.size(); i++)
        {
            GUID mat_guid;
            LoadObjMaterialResource(texture_path_guid_map, path.parent_path(), materials[i], path_in_project, mat_guid);
            material_guids.push_back(mat_guid);
        }

        // Save mesh data to binary file
        assetMesh.LoadFromTinyobj(attrib, shapes);
        std::filesystem::path mesh_data_path = GetAssetsDirectory() / path_in_project / (path.stem().string() + ".mesh_data");
        std::ofstream os(mesh_data_path, std::ios::binary);
        cereal::BinaryOutputArchive oarchive(os);
        oarchive(assetMesh);

        // Save mesh meta file
        mesh_json["guid"] = GUIDToString(mesh_guid);
        mesh_json["name"] = path.stem().string();
        mesh_json["type"] = "Mesh";
        mesh_json["submesh_count"] = assetMesh.GetSubmeshCount();

        std::filesystem::path mesh_json_path = GetAssetsDirectory() / path_in_project / (path.stem().string() + ".mesh_data.asset");
        std::ofstream mesh_file(mesh_json_path);
        if (mesh_file.is_open())
        {
            mesh_file << mesh_json.dump(4);
            mesh_file.close();
            AddAsset(mesh_guid, path_in_project / mesh_json_path.stem());
        }
        else
        {
            throw std::runtime_error("Failed to open mesh file");
        }

        // Save mesh prefab file
        nlohmann::json prefab_json;
        GUID prefab_guid = GenerateGUID();
        prefab_json["guid"] = GUIDToString(prefab_guid);
        prefab_json["name"] = path.stem().string();
        prefab_json["type"] = "GameObject";
        prefab_json["components"] = nlohmann::json::array();
        nlohmann::json mesh_component_json;
        mesh_component_json["type"] = "MeshComponent";
        mesh_component_json["mesh"] = GUIDToString(mesh_guid);
        mesh_component_json["materials"] = nlohmann::json::array();
        for (size_t s = 0; s < shapes.size(); s++)
        {
            // XXX: every face in a shape must have the same material
            size_t material_id = shapes[s].mesh.material_ids[0];
            mesh_component_json["materials"].push_back(GUIDToString(material_guids[material_id]));
        }
        prefab_json["components"].push_back(mesh_component_json);
        
        std::filesystem::path prefab_json_path = GetAssetsDirectory() / path_in_project / (path.stem().string() + ".prefab.asset");
        std::ofstream prefab_file(prefab_json_path);
        if (prefab_file.is_open())
        {
            prefab_file << prefab_json.dump(4);
            prefab_file.close();
            AddAsset(prefab_guid, path_in_project / prefab_json_path.stem());
        }
        else
        {
            throw std::runtime_error("Failed to open prefab file");
        }
    }

    void AssetManager::LoadObjMaterialResource(std::unordered_map<std::string, GUID> &texture_path_guid_map, const std::filesystem::path &parent_directory, const tinyobj::material_t &material, const std::filesystem::path &path_in_project, GUID &guid)
    {
        guid = GenerateGUID();
        nlohmann::json material_json;
        material_json["guid"] = GUIDToString(guid);
        material_json["name"] = material.name;
        material_json["type"] = "Shadeless";

        material_json["ambient"] = {material.ambient[0], material.ambient[1], material.ambient[2]};
        material_json["diffuse"] = {material.diffuse[0], material.diffuse[1], material.diffuse[2]};
        material_json["specular"] = {material.specular[0], material.specular[1], material.specular[2]};
        material_json["transmittance"] = {material.transmittance[0], material.transmittance[1], material.transmittance[2]};
        material_json["emission"] = {material.emission[0], material.emission[1], material.emission[2]};
        material_json["shininess"] = material.shininess;
        material_json["ior"] = material.ior;
        material_json["dissolve"] = material.dissolve;
        material_json["illum"] = material.illum;
        material_json["roughness"] = material.roughness;
        material_json["metallic"] = material.metallic;
        material_json["sheen"] = material.sheen;
        material_json["clearcoat_thickness"] = material.clearcoat_thickness;
        material_json["clearcoat_roughness"] = material.clearcoat_roughness;
        material_json["anisotropy"] = material.anisotropy;
        material_json["anisotropy_rotation"] = material.anisotropy_rotation;

        GUID tex_guid;
        // XXX: texture options are not implemented
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.ambient_texname, path_in_project, tex_guid))
            material_json["ambient_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.diffuse_texname, path_in_project, tex_guid))
            material_json["diffuse_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.specular_texname, path_in_project, tex_guid))
            material_json["specular_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.specular_highlight_texname, path_in_project, tex_guid))
            material_json["specular_highlight_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.bump_texname, path_in_project, tex_guid))
            material_json["bump_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.displacement_texname, path_in_project, tex_guid))
            material_json["displacement_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.alpha_texname, path_in_project, tex_guid))
            material_json["alpha_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.reflection_texname, path_in_project, tex_guid))
            material_json["reflection_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.roughness_texname, path_in_project, tex_guid))
            material_json["roughness_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.metallic_texname, path_in_project, tex_guid))
            material_json["metallic_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.sheen_texname, path_in_project, tex_guid))
            material_json["sheen_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.emissive_texname, path_in_project, tex_guid))
            material_json["emissive_tex"] = GUIDToString(tex_guid);
        if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.normal_texname, path_in_project, tex_guid))
            material_json["normal_tex"] = GUIDToString(tex_guid);

        std::filesystem::path json_path = GetAssetsDirectory() / path_in_project / (material.name + ".asset");
        std::ofstream material_file(json_path);
        if (material_file.is_open())
        {
            material_file << material_json.dump(4);
            material_file.close();
            AddAsset(guid, path_in_project / json_path.stem());
        }
        else
        {
            throw std::runtime_error("Failed to open material file");
        }
    }

    bool AssetManager::LoadObjTextureResource(std::unordered_map<std::string, GUID> &texture_path_guid_map, const std::filesystem::path &parent_directory, const std::string &filename, const std::filesystem::path &path_in_project, GUID &guid)
    {
        if(filename.empty())
            return false;
        if(texture_path_guid_map.find(filename) != texture_path_guid_map.end())
        {
            guid = texture_path_guid_map[filename];
            return true;
        }

        guid = GenerateGUID();
        nlohmann::json texture_json;
        texture_json["guid"] = GUIDToString(guid);
        texture_json["name"] = filename;
        texture_json["type"] = "ImmutableTexture2D";

        std::filesystem::path texpath_in_project = GetAssetsDirectory() / path_in_project / filename;
        std::filesystem::path tex_origin_path = parent_directory / filename;
        std::filesystem::copy_file(tex_origin_path, texpath_in_project);
        std::filesystem::path json_path = texpath_in_project.replace_filename(texpath_in_project.filename().string() + ".asset");
        std::ofstream tex_file(json_path);
        if (tex_file.is_open())
        {
            tex_file << texture_json.dump(4);
            tex_file.close();
            AddAsset(guid, path_in_project / json_path.stem());
        }
        else
        {
            throw std::runtime_error("Failed to open texture file");
        }
        texture_path_guid_map[filename] = guid;
        return true;
    }

} // namespace Engine