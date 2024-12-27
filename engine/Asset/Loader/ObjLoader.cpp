#include "ObjLoader.h"
#include <iostream>
#include <fstream>
#include <tiny_obj_loader.h>
#include <nlohmann/json.hpp>
#include <Asset/Mesh/MeshAsset.h>
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Texture/TextureAsset.h>
#include <MainClass.h>

namespace Engine
{
    ObjLoader::ObjLoader() {
        m_manager = MainClass::GetInstance()->GetAssetManager();
    }

    void ObjLoader::LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project)
    {
        // tinyobj::ObjReader reader;
        // reader.ParseFromFile(path.string());
        // if (!reader.Error().empty())
        // {
        //     throw std::runtime_error("TinyObjReader reports errors reading " + path.string());
        // }

        // if (!reader.Warning().empty())
        // {
        //     std::cerr << "TinyObjReader reports warnings reading " << path.string() << std::endl;
        //     std::cerr << "\t" << reader.Warning() << std::endl;
        // }

        // const tinyobj::attrib_t &attrib = reader.GetAttrib();
        // const std::vector<tinyobj::shape_t> &shapes = reader.GetShapes();
        // const std::vector<tinyobj::material_t> &materials = reader.GetMaterials();

        // nlohmann::json mesh_json;
        // std::vector<GUID> material_guids;
        // MeshAsset mesh_asset;
        // GUID mesh_guid = mesh_asset.GetGUID();

        // std::unordered_map<std::string, GUID> texture_path_guid_map;
        // for (size_t i = 0; i < materials.size(); i++)
        // {
        //     GUID mat_guid;
        //     LoadObjMaterialResource(texture_path_guid_map, path.parent_path(), materials[i], path_in_project, mat_guid);
        //     material_guids.push_back(mat_guid);
        // }

        // // Save mesh data to binary file
        // mesh_asset.LoadFromTinyobj(attrib, shapes);
        // std::filesystem::path mesh_data_path = m_manager.lock()->GetAssetsDirectory() / path_in_project / (path.stem().string() + ".mesh_data");
        // std::ofstream os(mesh_data_path, std::ios::binary);
        // cereal::BinaryOutputArchive oarchive(os);
        // oarchive(mesh_asset);

        // // Save mesh meta file
        // mesh_json["guid"] = mesh_guid.toString();
        // mesh_json["name"] = path.stem().string();
        // mesh_json["type"] = "Mesh";
        // mesh_json["submesh_count"] = mesh_asset.GetSubmeshCount();

        // std::filesystem::path mesh_json_path = m_manager.lock()->GetAssetsDirectory() / path_in_project / (path.stem().string() + ".mesh_data.asset");
        // std::ofstream mesh_file(mesh_json_path);
        // if (mesh_file.is_open())
        // {
        //     mesh_file << mesh_json.dump(4);
        //     mesh_file.close();
        //     m_manager.lock()->AddAsset(mesh_guid, path_in_project / mesh_json_path.stem());
        // }
        // else
        // {
        //     throw std::runtime_error("Failed to open mesh file");
        // }

        // // Save mesh prefab file
        // nlohmann::json prefab_json;
        // GUID prefab_guid = m_manager.lock()->GenerateGUID();
        // prefab_json["guid"] = prefab_guid.toString();
        // prefab_json["name"] = path.stem().string();
        // prefab_json["type"] = "GameObject";
        // prefab_json["components"] = nlohmann::json::array();
        // nlohmann::json mesh_component_json;
        // mesh_component_json["type"] = "MeshComponent";
        // mesh_component_json["mesh"] = mesh_guid.toString();
        // mesh_component_json["materials"] = nlohmann::json::array();
        // for (size_t s = 0; s < shapes.size(); s++)
        // {
        //     // XXX: every face in a shape must have the same material
        //     size_t material_id = shapes[s].mesh.material_ids[0];
        //     mesh_component_json["materials"].push_back(material_guids[material_id].toString());
        // }
        // prefab_json["components"].push_back(mesh_component_json);
        
        // std::filesystem::path prefab_json_path = m_manager.lock()->GetAssetsDirectory() / path_in_project / (path.stem().string() + ".prefab.asset");
        // std::ofstream prefab_file(prefab_json_path);
        // if (prefab_file.is_open())
        // {
        //     prefab_file << prefab_json.dump(4);
        //     prefab_file.close();
        //     m_manager.lock()->AddAsset(prefab_guid, path_in_project / prefab_json_path.stem());
        // }
        // else
        // {
        //     throw std::runtime_error("Failed to open prefab file");
        // }
    }

    void ObjLoader::LoadObjMaterialResource(std::unordered_map<std::string, GUID> &texture_path_guid_map, const std::filesystem::path &parent_directory, const tinyobj::material_t &material, const std::filesystem::path &path_in_project, GUID &guid)
    {
        // std::shared_ptr<MtlMaterialAsset> material_asset = std::make_shared<MtlMaterialAsset>();
        // guid = material_asset->GetGUID();
        // material_asset->m_name = material.name;

        // material_asset->m_ambient = {material.ambient[0], material.ambient[1], material.ambient[2]};
        // material_asset->m_diffuse = {material.diffuse[0], material.diffuse[1], material.diffuse[2]};
        // material_asset->m_specular = {material.specular[0], material.specular[1], material.specular[2]};
        // material_asset->m_transmittance = {material.transmittance[0], material.transmittance[1], material.transmittance[2]};
        // material_asset->m_emission = {material.emission[0], material.emission[1], material.emission[2]};
        // material_asset->m_shininess = material.shininess;
        // material_asset->m_ior = material.ior;
        // material_asset->m_dissolve = material.dissolve;
        // material_asset->m_illum = material.illum;

        // GUID tex_guid;
        // // XXX: texture options are not implemented
        // // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.ambient_texname, path_in_project, tex_guid))
        // //     material_asset->m_ambient_tex = 

        // material_asset->m_roughness = material.roughness;
        // material_asset->m_metallic = material.metallic;
        // material_asset->m_sheen = material.sheen;
        // material_asset->m_clearcoat_thickness = material.clearcoat_thickness;
        // material_asset->m_clearcoat_roughness = material.clearcoat_roughness;
        // material_asset->m_anisotropy = material.anisotropy;
        // material_asset->m_anisotropy_rotation = material.anisotropy_rotation;

        // GUID tex_guid;
        // // XXX: texture options are not implemented
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.ambient_texname, path_in_project, tex_guid))
        //     material_json["ambient_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.diffuse_texname, path_in_project, tex_guid))
        //     material_json["diffuse_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.specular_texname, path_in_project, tex_guid))
        //     material_json["specular_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.specular_highlight_texname, path_in_project, tex_guid))
        //     material_json["specular_highlight_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.bump_texname, path_in_project, tex_guid))
        //     material_json["bump_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.displacement_texname, path_in_project, tex_guid))
        //     material_json["displacement_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.alpha_texname, path_in_project, tex_guid))
        //     material_json["alpha_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.reflection_texname, path_in_project, tex_guid))
        //     material_json["reflection_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.roughness_texname, path_in_project, tex_guid))
        //     material_json["roughness_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.metallic_texname, path_in_project, tex_guid))
        //     material_json["metallic_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.sheen_texname, path_in_project, tex_guid))
        //     material_json["sheen_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.emissive_texname, path_in_project, tex_guid))
        //     material_json["emissive_tex"] = tex_guid.toString();
        // if(LoadObjTextureResource(texture_path_guid_map, parent_directory, material.normal_texname, path_in_project, tex_guid))
        //     material_json["normal_tex"] = tex_guid.toString();

        // std::filesystem::path json_path = m_manager.lock()->GetAssetsDirectory() / path_in_project / (material.name + ".asset");
        // std::ofstream material_file(json_path);
        // if (material_file.is_open())
        // {
        //     material_file << material_json.dump(4);
        //     material_file.close();
        //     m_manager.lock()->AddAsset(guid, path_in_project / json_path.stem());
        // }
        // else
        // {
        //     throw std::runtime_error("Failed to open material file");
        // }
    }

    bool ObjLoader::LoadObjTextureResource(std::unordered_map<std::string, GUID> &texture_path_guid_map, const std::filesystem::path &parent_directory, const std::string &filename, const std::filesystem::path &path_in_project, GUID &guid)
    {
        if(filename.empty())
            return false;
        if(texture_path_guid_map.find(filename) != texture_path_guid_map.end())
        {
            guid = texture_path_guid_map[filename];
            return true;
        }

        

        guid = m_manager.lock()->GenerateGUID();
        nlohmann::json texture_json;
        texture_json["guid"] = guid.toString();
        texture_json["name"] = filename;
        texture_json["type"] = "ImmutableTexture2D";

        std::filesystem::path texpath_in_project = m_manager.lock()->GetAssetsDirectory() / path_in_project / filename;
        std::filesystem::path tex_origin_path = parent_directory / filename;
        std::filesystem::copy_file(tex_origin_path, texpath_in_project);
        std::filesystem::path json_path = texpath_in_project.replace_filename(texpath_in_project.filename().string() + ".asset");
        std::ofstream tex_file(json_path);
        if (tex_file.is_open())
        {
            tex_file << texture_json.dump(4);
            tex_file.close();
            m_manager.lock()->AddAsset(guid, path_in_project / json_path.stem());
        }
        else
        {
            throw std::runtime_error("Failed to open texture file");
        }
        texture_path_guid_map[filename] = guid;
        return true;
    }
}
