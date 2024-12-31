#ifndef ASSET_LOADER_OBJLOADER_INCLUDED
#define ASSET_LOADER_OBJLOADER_INCLUDED

#include <unordered_map>
#include <memory>
#include <filesystem>

namespace Engine
{
    class AssetManager;

    class ObjLoader
    {
    public:
        ObjLoader();
        virtual ~ObjLoader() = default;

    public:
        /// @brief Load an external obj resource, copy to the project asset directory, and create a meta file
        /// @param path path to the external obj resource
        /// @param path_in_project path to the output asset directory relative to the project asset directory
        void LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project);
    
    protected:
        std::weak_ptr <AssetManager> m_manager{};
    };
}

#endif // ASSET_LOADER_OBJLOADER_INCLUDED
