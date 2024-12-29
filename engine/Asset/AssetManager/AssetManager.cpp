#include "AssetManager.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <cassert>

#include "Asset/Loader/ObjLoader.h"

namespace Engine
{
    void AssetManager::LoadProject(std::filesystem::path path)
    {
        if (!std::filesystem::exists(path))
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
                    // TODO: need better way to check if it is an asset file
                    if (json_data.contains("%main_id"))
                    {
                        std::string str_id = json_data["%main_id"].get<std::string>();
                        if (json_data["%data"].contains(str_id) && json_data["%data"][str_id].contains("Asset::m_guid"))
                        {
                            GUID guid(json_data["%data"][str_id]["Asset::m_guid"].get<std::string>());
                            AddAsset(guid, relative_path);
                        }
                    }
                    file.close();
                }
            }
        }
    }

    void AssetManager::ImportExternalResource(std::filesystem::path resourcePath, std::filesystem::path path_in_project)
    {
        if (!std::filesystem::exists(GetAssetsDirectory() / path_in_project))
            std::filesystem::create_directory(GetAssetsDirectory() / path_in_project);
        std::string extension = resourcePath.extension().string();
        if (extension == ".obj")
        {
            ObjLoader loader;
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
        if (m_assets_map.find(guid) != m_assets_map.end())
            throw std::runtime_error("asset GUID already exists");
        m_assets_map[guid] = path;
    }

    void AssetManager::AddToLoadingQueue(std::shared_ptr<Asset> asset)
    {
        m_loading_queue.push(asset);
    }

    void AssetManager::LoadAssetsInQueue()
    {
        while (!m_loading_queue.empty())
        {
            auto asset = m_loading_queue.front();
            auto path = GetAssetPath(asset->GetGUID());

            Serialization::Archive archive;
            archive.load_from_file(path);
            archive.prepare_load();
            asset->load_asset_from_archive(archive);

            m_loading_queue.pop();
        }
    }

    std::shared_ptr<Asset> AssetManager::LoadAssetImmediately(const GUID &guid)
    {
        auto path = GetAssetPath(guid);
        Serialization::Archive archive;
        archive.load_from_file(path);
        auto type = Reflection::GetType(archive.GetMainDataProperty("%type").get<std::string>());
        assert(type->m_reflectable);
        auto var = type->CreateInstance(Serialization::__SerializationMarker__{});
        std::shared_ptr<Asset> ret = std::shared_ptr<Asset>(static_cast<Asset *>(var.GetDataPtr()));
        archive.prepare_load();
        ret->load_asset_from_archive(archive);
        return ret;
    }
} // namespace Engine
