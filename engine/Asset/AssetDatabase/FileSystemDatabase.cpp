#include "FileSystemDatabase.h"
#include <Asset/AssetRef.h>
#include <Reflection/Archive.h>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {
    bool GetGUID(const Engine::Serialization::Archive &archive, Engine::GUID &out_guid) {
        const nlohmann::json &json_data = archive.m_context->json;
        if (json_data["%main_data"].contains("Asset::m_guid")) {
            out_guid = Engine::GUID(json_data["%main_data"]["Asset::m_guid"].get<std::string>());
            return true;
        }
        return false;
    }

    bool GetGUID(const std::filesystem::path &asset_path, Engine::GUID &out_guid) {
        std::ifstream file(asset_path);
        if (file.is_open()) {
            nlohmann::json json_data = nlohmann::json::parse(file);
            if (json_data["%main_data"].contains("Asset::m_guid")) {
                out_guid = Engine::GUID(json_data["%main_data"]["Asset::m_guid"].get<std::string>());
                file.close();
                return true;
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
            if (json_data["%main_data"].contains("Asset::m_guid")) {
                out_info.guid = Engine::GUID(json_data["%main_data"]["Asset::m_guid"].get<std::string>());
            } else {
                success = false;
            }
            if (json_data["%main_data"].contains("%type")) {
                out_info.type_name = json_data["%main_data"]["%type"].get<std::string>();
            } else {
                success = false;
            }
            file.close();
        } else {
            success = false;
        }
        return success;
    }

    bool IsBuiltinAssetPath(const Engine::AssetPath &path) {
        auto it = path.begin();
        return it != path.end() && it->string() == "~";
    }

    bool IsPathInside(const std::filesystem::path &parent, const std::filesystem::path &child) {
        std::error_code ec;
        const auto canonical_parent = std::filesystem::weakly_canonical(parent, ec);
        if (ec) return false;
        ec.clear();
        const auto canonical_child = std::filesystem::weakly_canonical(child, ec);
        if (ec) return false;
        ec.clear();
        auto rel = std::filesystem::relative(canonical_child, canonical_parent, ec);
        if (ec) return false;
        if (rel.empty()) return true;
        const std::string rel_str = rel.generic_string();
        return rel_str != "." && !(rel_str.size() >= 2 && rel_str[0] == '.' && rel_str[1] == '.');
    }

    bool EnsurePathExists(const std::filesystem::path &path) {
        std::filesystem::path path_obj = path;
        if (std::filesystem::exists(path_obj)) {
            return true;
        }
        std::filesystem::path dir_path = path_obj.parent_path();

        if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
            std::error_code ec;
            if (std::filesystem::create_directories(dir_path, ec)) {
                return true;
            } else if (ec) {
                return false;
            }
        }
        return true;
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

    AssetRef FileSystemDatabase::GetNewAssetRef(const AssetPath &path) const {
        if (m_path_to_guid.find(path) == m_path_to_guid.end()) throw std::runtime_error("Asset not found");
        return AssetRef(m_path_to_guid.at(path));
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
        EnsurePathExists(json_path);
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

    bool FileSystemDatabase::CreateDirectory(const AssetPath &path) {
        if (IsBuiltinAssetPath(path)) {
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(path.to_absolute_path(), ec);
        if (ec) {
            return false;
        }
        return std::filesystem::exists(path.to_absolute_path());
    }

    bool FileSystemDatabase::MovePath(const AssetPath &from, const AssetPath &to) {
        if (IsBuiltinAssetPath(from) || IsBuiltinAssetPath(to)) {
            return false;
        }

        const auto from_abs = from.to_absolute_path();
        const auto to_abs = to.to_absolute_path();
        std::error_code ec;
        if (!std::filesystem::exists(from_abs, ec)) {
            return false;
        }
        ec.clear();
        if (std::filesystem::exists(to_abs, ec)) {
            return false;
        }
        ec.clear();

        if (std::filesystem::is_directory(from_abs, ec) && IsPathInside(from_abs, to_abs)) {
            return false;
        }

        std::vector<std::pair<GUID, AssetPath>> remapped_assets;
        remapped_assets.reserve(m_assets_map.size());
        for (const auto &[guid, path] : m_assets_map) {
            const auto old_abs = path.to_absolute_path();
            if (IsPathInside(from_abs, old_abs)) {
                auto rel = std::filesystem::relative(old_abs, from_abs, ec);
                if (ec) {
                    continue;
                }
                AssetPath new_path(*this);
                new_path.from_absolute_path(to_abs / rel);
                remapped_assets.emplace_back(guid, new_path);
            }
            ec.clear();
        }

        std::filesystem::create_directories(to_abs.parent_path(), ec);
        ec.clear();
        std::filesystem::rename(from_abs, to_abs, ec);
        if (ec) {
            return false;
        }

        for (const auto &[guid, new_path] : remapped_assets) {
            auto old_it = m_assets_map.find(guid);
            if (old_it == m_assets_map.end()) {
                continue;
            }
            m_path_to_guid.erase(old_it->second);
            old_it->second = new_path;
            m_path_to_guid[new_path] = guid;
        }

        return true;
    }

    bool FileSystemDatabase::DeletePath(const AssetPath &path) {
        if (IsBuiltinAssetPath(path)) {
            return false;
        }

        const auto abs_path = path.to_absolute_path();
        std::error_code ec;
        if (!std::filesystem::exists(abs_path, ec)) {
            return false;
        }

        std::vector<GUID> to_remove;
        if (std::filesystem::is_directory(abs_path, ec)) {
            for (const auto &[guid, asset_path] : m_assets_map) {
                if (IsPathInside(abs_path, asset_path.to_absolute_path())) {
                    to_remove.push_back(guid);
                }
            }
            std::filesystem::remove_all(abs_path, ec);
        } else {
            if (abs_path.extension() == k_asset_file_extension) {
                GUID guid;
                if (GetGUID(abs_path, guid)) {
                    to_remove.push_back(guid);
                }
            }
            std::filesystem::remove(abs_path, ec);
        }

        if (ec) {
            return false;
        }

        for (const auto &guid : to_remove) {
            auto it = m_assets_map.find(guid);
            if (it == m_assets_map.end()) {
                continue;
            }
            m_path_to_guid.erase(it->second);
            m_assets_map.erase(it);
        }

        return true;
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
