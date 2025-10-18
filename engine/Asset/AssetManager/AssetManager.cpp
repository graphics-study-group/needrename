#include "AssetManager.h"

#include "Asset/Loader/ObjLoader.h"
#include <Asset/Asset.h>
#include <Asset/AssetRef.h>
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Engine {
    void AssetManager::SetBuiltinAssetPath(const std::filesystem::path &path) {
        m_builtin_asset_path = path;
    }

    void AssetManager::LoadBuiltinAssets() {
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::recursive_directory_iterator(m_builtin_asset_path)) {
            std::filesystem::path relative_path = std::filesystem::relative(entry.path(), m_builtin_asset_path);
            if (relative_path.extension() == ".asset") {
                std::ifstream file(entry.path());
                if (file.is_open()) {
                    nlohmann::json json_data = nlohmann::json::parse(file);
                    // TODO: need better way to check if it is an asset file
                    if (json_data.contains("%main_id")) {
                        std::string str_id = json_data["%main_id"].get<std::string>();
                        if (json_data["%data"].contains(str_id)
                            && json_data["%data"][str_id].contains("Asset::m_guid")) {
                            GUID guid(json_data["%data"][str_id]["Asset::m_guid"].get<std::string>());
                            AddAsset(guid, "~" / relative_path);
                        }
                    }
                    file.close();
                }
            }
        }
    }

    void AssetManager::LoadProject(std::filesystem::path path) {
        if (!std::filesystem::exists(path)) throw std::runtime_error("Project path does not exist");
        m_project_path = path;
        auto asset_path = path / "assets";
        if (!std::filesystem::exists(asset_path)) std::filesystem::create_directory(asset_path);
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::recursive_directory_iterator(asset_path)) {
            std::filesystem::path relative_path = std::filesystem::relative(entry.path(), asset_path);
            if (relative_path.extension() == ".asset") {
                std::ifstream file(entry.path());
                if (file.is_open()) {
                    nlohmann::json json_data = nlohmann::json::parse(file);
                    // TODO: need better way to check if it is an asset file
                    if (json_data.contains("%main_id")) {
                        std::string str_id = json_data["%main_id"].get<std::string>();
                        if (json_data["%data"].contains(str_id)
                            && json_data["%data"][str_id].contains("Asset::m_guid")) {
                            GUID guid(json_data["%data"][str_id]["Asset::m_guid"].get<std::string>());
                            AddAsset(guid, relative_path);
                        }
                    }
                    file.close();
                }
            }
        }
    }

    void AssetManager::ImportExternalResource(
        std::filesystem::path resourcePath, std::filesystem::path path_in_project
    ) {
        if (!std::filesystem::exists(GetAssetsDirectory() / path_in_project))
            std::filesystem::create_directory(GetAssetsDirectory() / path_in_project);
        std::string extension = resourcePath.extension().string();
        if (extension == ".obj") {
            ObjLoader loader;
            loader.LoadObjResource(resourcePath, path_in_project);
        } else {
            throw std::runtime_error("Unsupported file format");
        }
    }

    std::filesystem::path AssetManager::GetAssetPath(GUID guid) const {
        auto it = m_assets_map.find(guid);
        if (it != m_assets_map.end()) {
            if (it->second.begin()->string() == "~")
                return m_builtin_asset_path / it->second.relative_path().string().substr(2);
            else return GetAssetsDirectory() / it->second;
        } else throw std::runtime_error("Asset not found");
    }

    std::filesystem::path AssetManager::GetAssetPath(const std::shared_ptr<Asset> &asset) const {
        return GetAssetPath(asset->GetGUID());
    }

    std::shared_ptr<AssetRef> AssetManager::GetNewAssetRef(const std::filesystem::path &path) {
        if (m_path_to_guid.find(path) == m_path_to_guid.end()) return nullptr;
        return std::make_shared<AssetRef>(m_path_to_guid[path]);
    }

    void AssetManager::AddAsset(const GUID &guid, const std::filesystem::path &path) {
        if (m_assets_map.find(guid) != m_assets_map.end()) throw std::runtime_error("asset GUID already exists");
        m_assets_map[guid] = path;
        m_path_to_guid[path] = guid;
    }

    void AssetManager::AddToLoadingQueue(std::shared_ptr<AssetRef> asset_ref) {
        m_loading_queue.push(asset_ref);
    }

    void AssetManager::LoadAssetsInQueue() {
        while (!m_loading_queue.empty()) {
            auto asset_ref = m_loading_queue.front();
            auto path = GetAssetPath(asset_ref->GetGUID());

            Serialization::Archive archive;
            archive.load_from_file(path);
            archive.prepare_load();

            auto asset_type = Reflection::GetType(archive.GetMainDataProperty("%type").get<std::string>());
            assert(asset_type->IsReflectable());
            // TODO: asset memory management
            auto var = asset_type->CreateInstance(Serialization::SerializationMarker{});
            auto asset_ptr = std::shared_ptr<Asset>(static_cast<Asset *>(var.GetDataPtr()));
            var.MarkNeedFree(false);
            asset_ptr->load_asset_from_archive(archive);
            asset_ref->m_asset = asset_ptr;

            m_loading_queue.pop();
        }
    }

    std::shared_ptr<Asset> AssetManager::LoadAssetImmediately(const GUID &guid) {
        auto path = GetAssetPath(guid);
        Serialization::Archive archive;
        archive.load_from_file(path);
        auto type = Reflection::GetType(archive.GetMainDataProperty("%type").get<std::string>());
        assert(type->IsReflectable());
        auto var = type->CreateInstance(Serialization::SerializationMarker{});
        std::shared_ptr<Asset> ret = std::shared_ptr<Asset>(static_cast<Asset *>(var.GetDataPtr()));
        var.MarkNeedFree(false);
        archive.prepare_load();
        ret->load_asset_from_archive(archive);
        return ret;
    }

    void AssetManager::LoadAssetImmediately(std::shared_ptr<AssetRef> asset_ref) {
        auto guid = asset_ref->GetGUID();
        asset_ref->m_asset = LoadAssetImmediately(guid);
    }
} // namespace Engine
