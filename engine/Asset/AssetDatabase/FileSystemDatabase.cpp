#include "FileSystemDatabase.h"
#include <Asset/AssetRef.h>
#include <Reflection/Archive.h>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {
    bool GetGUID(const Engine::Serialization::Archive &archive, Engine::GUID &out_guid) {
        const nlohmann::json &json_data = archive.m_context->json;
        if (json_data.contains("%main_id")) {
            std::string str_id = json_data["%main_id"].get<std::string>();
            if (json_data["%data"].contains(str_id) && json_data["%data"][str_id].contains("Asset::m_guid")) {
                out_guid = Engine::GUID(json_data["%data"][str_id]["Asset::m_guid"].get<std::string>());
                return true;
            }
        }
        return false;
    }

    bool GetGUID(const std::filesystem::path &asset_path, Engine::GUID &out_guid) {
        std::ifstream file(asset_path);
        if (file.is_open()) {
            nlohmann::json json_data = nlohmann::json::parse(file);
            if (json_data.contains("%main_id")) {
                std::string str_id = json_data["%main_id"].get<std::string>();
                if (json_data["%data"].contains(str_id) && json_data["%data"][str_id].contains("Asset::m_guid")) {
                    out_guid = Engine::GUID(json_data["%data"][str_id]["Asset::m_guid"].get<std::string>());
                    file.close();
                    return true;
                }
            }
            file.close();
        }
        return false;
    }

    bool GetAssetInfo(const std::filesystem::path &asset_path, Engine::FileSystemDatabase::AssetInfo &out_info) {
        bool success = true;
        std::ifstream file(asset_path);
        if (file.is_open()) {
            out_info.path = asset_path;
            out_info.is_directory = false;
            nlohmann::json json_data = nlohmann::json::parse(file);
            if (json_data.contains("%main_id")) {
                std::string str_id = json_data["%main_id"].get<std::string>();
                if (json_data["%data"].contains(str_id) && json_data["%data"][str_id].contains("Asset::m_guid")) {
                    out_info.guid = Engine::GUID(json_data["%data"][str_id]["Asset::m_guid"].get<std::string>());
                } else {
                    success = false;
                }
                if (json_data["%data"].contains(str_id) && json_data["%data"][str_id].contains("%type")) {
                    out_info.type_name = json_data["%data"][str_id]["%type"].get<std::string>();
                } else {
                    success = false;
                }
            } else {
                success = false;
            }
            file.close();
        } else {
            success = false;
        }
        return success;
    }
} // namespace

namespace Engine {
    std::filesystem::path FileSystemDatabase::GetAssetPath(GUID guid) const {
        auto it = m_assets_map.find(guid);
        if (it != m_assets_map.end()) {
            return it->second;
        } else {
            throw std::runtime_error("Asset not found");
        }
    }

    std::shared_ptr<AssetRef> FileSystemDatabase::GetNewAssetRef(const std::filesystem::path &path) const {
        std::string path_str = path.lexically_normal().generic_string();
        if (m_path_to_guid.find(path_str) == m_path_to_guid.end()) return nullptr;
        return std::make_shared<AssetRef>(m_path_to_guid.at(path_str));
    }

    void FileSystemDatabase::AddAsset(const GUID &guid, const std::filesystem::path &path) {
        if (m_assets_map.find(guid) != m_assets_map.end()) throw std::runtime_error("asset GUID already exists");
        std::string path_str = path.lexically_normal().generic_string();
        m_assets_map[guid] = path_str;
        m_path_to_guid[path_str] = guid;
    }

    void FileSystemDatabase::SaveArchive(Serialization::Archive &archive, GUID guid) {
        SaveArchive(archive, GetAssetPath(guid));
    }

    void FileSystemDatabase::LoadArchive(Serialization::Archive &archive, GUID guid) {
        LoadArchive(archive, GetAssetPath(guid));
    }

    void FileSystemDatabase::SaveArchive(Serialization::Archive &archive, const std::filesystem::path &path) {
        GUID guid;
        if (!GetGUID(archive, guid)) {
            throw std::runtime_error("Failed to get GUID from asset file: " + path.string());
        }
        if (m_assets_map.find(guid) == m_assets_map.end()) {
            AddAsset(guid, path);
        }
        auto json_path = ProjectPathToFilesystemPath(path);
        archive.save_to_file(json_path.replace_extension(""));
    }

    void FileSystemDatabase::LoadArchive(Serialization::Archive &archive, const std::filesystem::path &path) {
        auto json_path = ProjectPathToFilesystemPath(path);
        archive.load_from_file(json_path.replace_extension(""));
    }

    std::vector<FileSystemDatabase::AssetInfo> FileSystemDatabase::ListDirectory(
        const std::filesystem::path &path
    ) const {
        std::vector<AssetInfo> assets;
        for (auto &entry : std::filesystem::directory_iterator(ProjectPathToFilesystemPath(path))) {
            AssetInfo info;
            if (entry.is_directory()) {
                info.path = FilesystemPathToProjectPath(entry.path());
                info.is_directory = true;
                assets.push_back(info);
            } else if (entry.is_regular_file() && entry.path().extension() == k_asset_file_extension) {
                if (GetAssetInfo(entry.path(), info)) {
                    assets.push_back(info);
                }
            }
        }
        return assets;
    }

    std::filesystem::path FileSystemDatabase::GetAssetsDirectory() const {
        return m_project_asset_path;
    }

    void FileSystemDatabase::LoadBuiltinAssets(const std::filesystem::path &path) {
        m_builtin_asset_path = path.generic_string();
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::recursive_directory_iterator(m_builtin_asset_path)) {
            std::filesystem::path relative_path = std::filesystem::relative(entry.path(), m_builtin_asset_path);
            if (relative_path.extension() == ".asset") {
                std::ifstream file(entry.path());
                if (file.is_open()) {
                    GUID guid;
                    if (GetGUID(entry.path(), guid)) {
                        AddAsset(guid, "~" / relative_path);
                    }
                    file.close();
                }
            }
        }
    }

    void FileSystemDatabase::LoadProjectAssets(const std::filesystem::path &path) {
        m_project_asset_path = path.generic_string();
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::recursive_directory_iterator(m_project_asset_path)) {
            std::filesystem::path relative_path = std::filesystem::relative(entry.path(), m_project_asset_path);
            if (relative_path.extension() == ".asset") {
                std::ifstream file(entry.path());
                if (file.is_open()) {
                    GUID guid;
                    if (GetGUID(entry.path(), guid)) {
                        AddAsset(guid, "/" / relative_path);
                    }
                    file.close();
                }
            }
        }
    }

    std::filesystem::path FileSystemDatabase::ProjectPathToFilesystemPath(
        const std::filesystem::path &project_path
    ) const {
        // Case 1: "~" prefixed project path (e.g., "~/a/b")
        auto it = project_path.begin();
        if (it != project_path.end() && it->string() == "~") {
            return m_builtin_asset_path / (std::filesystem::relative(project_path, "~"));
        }
        // Case 2: "/" prefixed project path (e.g., "/a/b")
        if (project_path.has_root_directory()) {
            return m_project_asset_path / project_path.relative_path();
        }
        // Case 3: relative project path (e.g., "a/b")
        return m_project_asset_path / project_path;
    }

    std::filesystem::path FileSystemDatabase::FilesystemPathToProjectPath(const std::filesystem::path &fs_path) const {
        if (fs_path.string().starts_with(m_builtin_asset_path.string())) {
            return ("~" / std::filesystem::relative(fs_path, m_builtin_asset_path)).lexically_normal();
        } else {
            return ("/" / std::filesystem::relative(fs_path, m_project_asset_path)).lexically_normal();
        }
    }
} // namespace Engine
