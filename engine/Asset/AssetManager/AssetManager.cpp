#include "AssetManager.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <cassert>
#include <iostream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>

#include "Asset/Loader/ObjLoader.h"

namespace Engine
{
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
                    if (json_data.contains("%guid"))
                    {
                        GUID guid(json_data["%guid"]);
                        AddAsset(guid, relative_path.parent_path() / relative_path.stem());
                    }
                    file.close();
                }
                else
                    throw std::runtime_error("Failed to open asset file");
            }
        }
    }

    void AssetManager::LoadExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project)
    {
        if (!std::filesystem::exists(GetAssetsDirectory() / path_in_project))
            std::filesystem::create_directory(GetAssetsDirectory() / path_in_project);
        std::string extension = resourcePath.extension().string();
        if (extension == ".obj")
        {
            ObjLoader loader(this->shared_from_this());
            loader.LoadObjResource(resourcePath, path_in_project);
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
        auto it = m_assets_map.find(guid);
        if (it != m_assets_map.end())
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
        if(m_assets_map.find(guid) != m_assets_map.end())
            throw std::runtime_error("asset GUID already exists");
        m_assets_map[guid] = path;
    }

    void AssetManager::AddToLoadingQueue(const GUID &guid)
    {
        m_loading_queue.push(guid);
    }

    void AssetManager::AddToLoadingQueue(const Asset &asset)
    {
        m_loading_queue.push(asset.GetGUID());
    }

    void AssetManager::LoadAssetsInQueue()
    {
        throw std::runtime_error("Not implemented");
    }
} // namespace Engine
