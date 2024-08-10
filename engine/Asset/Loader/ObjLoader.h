#ifndef ASSET_LOADER_OBJLOADER_INCLUDED
#define ASSET_LOADER_OBJLOADER_INCLUDED

#include <filesystem>
#include <unordered_map>

#include "Asset/AssetManager/AssetManager.h"

namespace Engine
{
    class ObjLoader
    {
    public:
        ObjLoader() = default;
        virtual ~ObjLoader() = default;

    public:
        /// @brief Load an external obj resource, copy to the project asset directory, and create a meta file
        /// @param path path to the external obj resource
        /// @param path_in_project path to the output asset directory relative to the project asset directory
        void LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project);
    
    private:
        /// @brief Load an external material resource, read from .mtl file associated with an obj file. create a meta file at @param path_in_project
        /// @param texture_path_guid_map to mark the texture files that have been loaded, prevent loading the same texture multiple times
        /// @param parent_directory path to the parent directory of the obj file
        /// @param material material data read from tinyobjloader
        /// @param path_in_project path to the output asset directory relative to the project asset directory
        /// @param guid to store the GUID of the material
        void LoadObjMaterialResource(std::unordered_map<std::string, GUID> &texture_path_guid_map, const std::filesystem::path &parent_directory, const tinyobj::material_t &material, const std::filesystem::path &path_in_project, GUID &guid);

        /// @brief Load an external texture resource, read from .mtl file associated with an obj file. create a meta file at @param path_in_project
        /// @param texture_path_guid_map to mark the texture files that have been loaded, prevent loading the same texture multiple times
        /// @param parent_directory path to the parent directory of the obj file
        /// @param filename file name of the texture file
        /// @param path_in_project path to the output asset directory relative to the project asset directory
        /// @param guid to store the GUID of the texture
        /// @return true if the texture is loaded successfully
        bool LoadObjTextureResource(std::unordered_map<std::string, GUID> &texture_path_guid_map, const std::filesystem::path &parent_directory, const std::string &filename, const std::filesystem::path &path_in_project, GUID &guid);
    };
}

#endif // ASSET_LOADER_OBJLOADER_INCLUDED