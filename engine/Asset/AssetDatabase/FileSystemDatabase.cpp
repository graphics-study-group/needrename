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

    bool GetAssetInfo(
        const std::filesystem::path &asset_absolute_path, Engine::FileSystemDatabase::AssetInfo &out_info
    ) {
        bool success = true;
        std::ifstream file(asset_absolute_path);
        if (file.is_open()) {
            out_info.path.from_absolute_path(asset_absolute_path);
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
    AssetPath::AssetPath(const FileSystemDatabase &db) : std::filesystem::path(), m_database(db) {
    }

    AssetPath::AssetPath(const FileSystemDatabase &db, const std::filesystem::path &path) :
        std::filesystem::path(path), m_database(db) {
        static_cast<std::filesystem::path &>(*this) = this->lexically_normal();
    }

    AssetPath &AssetPath::operator=(const AssetPath &other) {
        assert(&m_database == &other.m_database && "Cannot assign AssetPath from different FileSystemDatabase");
        static_cast<std::filesystem::path &>(*this) = static_cast<const std::filesystem::path &>(other);
        return *this;
    }

    bool AssetPath::operator==(const AssetPath &other) const {
        assert(&m_database == &other.m_database && "Cannot compare AssetPath from different FileSystemDatabase");
        return static_cast<const std::filesystem::path &>(*this) == static_cast<const std::filesystem::path &>(other);
    }

    std::size_t AssetPath::Hash::operator()(const AssetPath &p) const {
        return std::hash<std::filesystem::path>{}(static_cast<const std::filesystem::path &>(p));
    }

    std::filesystem::path AssetPath::to_absolute_path() const {
        auto project_asset_path = m_database.GetProjectAssetsPath();
        auto builtin_asset_path = m_database.GetBuiltinAssetsPath();
        // Case 1: "~" prefixed project path (e.g., "~/a/b")
        auto it = this->begin();
        if (it != this->end() && it->string() == "~") {
            assert(!builtin_asset_path.empty());
            return builtin_asset_path / (std::filesystem::relative(*this, "~"));
        }
        // Case 2: "/" prefixed project path (e.g., "/a/b")
        if (this->has_root_directory()) {
            assert(!project_asset_path.empty());
            return project_asset_path / this->relative_path();
        }
        // Case 3: relative project path (e.g., "a/b")
        return project_asset_path / *this;
    }

    void AssetPath::from_absolute_path(const std::filesystem::path &absolute_path) {
        auto project_asset_path = m_database.GetProjectAssetsPath();
        auto builtin_asset_path = m_database.GetBuiltinAssetsPath();
        if (absolute_path.string().starts_with(builtin_asset_path.string())) {
            static_cast<std::filesystem::path &>(*this) =
                ("~" / std::filesystem::relative(absolute_path, builtin_asset_path)).lexically_normal();
        } else {
            static_cast<std::filesystem::path &>(*this) =
                ("/" / std::filesystem::relative(absolute_path, project_asset_path)).lexically_normal();
        }
    }

    AssetPath AssetPath::parent_path() const {
        AssetPath parent(m_database);
        static_cast<std::filesystem::path &>(parent) = std::filesystem::path::parent_path();
        return parent;
    }

    AssetPath FileSystemDatabase::GetAssetPath(GUID guid) const {
        auto it = m_assets_map.find(guid);
        if (it != m_assets_map.end()) {
            return it->second;
        } else {
            throw std::runtime_error("Asset not found");
        }
    }

    std::shared_ptr<AssetRef> FileSystemDatabase::GetNewAssetRef(const AssetPath &path) const {
        if (m_path_to_guid.find(path) == m_path_to_guid.end()) return nullptr;
        return std::make_shared<AssetRef>(m_path_to_guid.at(path));
    }

    void FileSystemDatabase::AddAsset(const GUID &guid, const AssetPath &path) {
        if (m_assets_map.find(guid) != m_assets_map.end()) throw std::runtime_error("asset GUID already exists");
        m_assets_map.emplace(guid, path);
        m_path_to_guid[path] = guid;
    }

    void FileSystemDatabase::SaveArchive(Serialization::Archive &archive, GUID guid) {
        SaveArchive(archive, GetAssetPath(guid));
    }

    void FileSystemDatabase::LoadArchive(Serialization::Archive &archive, GUID guid) {
        LoadArchive(archive, GetAssetPath(guid));
    }

    void FileSystemDatabase::SaveArchive(Serialization::Archive &archive, const AssetPath &path) {
        GUID guid;
        if (!GetGUID(archive, guid)) {
            throw std::runtime_error("Failed to get GUID from asset file: " + path.generic_string());
        }
        if (m_assets_map.find(guid) == m_assets_map.end()) {
            AddAsset(guid, path);
        }
        auto json_path = path.to_absolute_path();
        archive.save_to_file(json_path.replace_extension(""));
    }

    void FileSystemDatabase::LoadArchive(Serialization::Archive &archive, const AssetPath &path) {
        auto json_path = path.to_absolute_path();
        archive.load_from_file(json_path.replace_extension(""));
    }

    std::vector<FileSystemDatabase::AssetInfo> FileSystemDatabase::ListDirectory(const AssetPath &path) const {
        std::vector<AssetInfo> assets;
        for (auto &entry : std::filesystem::directory_iterator(path.to_absolute_path())) {
            AssetInfo info{.path = AssetPath(*this)};
            if (entry.is_directory()) {
                auto asset_path = AssetPath(*this);
                asset_path.from_absolute_path(entry.path());
                info.path = asset_path;
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

    const std::filesystem::path &FileSystemDatabase::GetProjectAssetsPath() const {
        return m_project_asset_path;
    }

    const std::filesystem::path &FileSystemDatabase::GetBuiltinAssetsPath() const {
        return m_builtin_asset_path;
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
                        AddAsset(guid, AssetPath(*this, "~" / relative_path));
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
                        AddAsset(guid, AssetPath(*this, "/" / relative_path));
                    }
                    file.close();
                }
            }
        }
    }
} // namespace Engine
