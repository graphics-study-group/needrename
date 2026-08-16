#include "FileSystemDatabase.h"
#include <Asset/AssetRef.h>

#include <AnnoRefl/Archive.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
    bool GetGUID(const AnnoRefl::Archive &archive, Engine::GUID &out_guid) {
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
        const std::string rel_str = rel.generic_string();
        if (rel_str.empty() || rel_str == ".") return true;
        return !(rel_str.size() >= 2 && rel_str[0] == '.' && rel_str[1] == '.');
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
    bool FileSystemDatabase::IsWritableScheme(const AssetPath &path) const {
        const auto it = m_mounts.find(std::string(path.GetScheme()));
        return it != m_mounts.end() && it->second.writable;
    }

    void FileSystemDatabase::RegisterScheme(std::string_view scheme, const std::filesystem::path &root, bool writable) {
        m_mounts[std::string(scheme)] = Mount{.root = root, .writable = writable};
    }

    std::filesystem::path FileSystemDatabase::ToAbsolutePath(const AssetPath &path) const {
        const auto it = m_mounts.find(std::string(path.GetScheme()));
        if (it == m_mounts.end()) {
            throw std::runtime_error("Unmounted asset scheme: " + std::string(path.GetScheme()));
        }
        return it->second.root / path.GetSubPath();
    }

    AssetPath FileSystemDatabase::FromAbsolutePath(const std::filesystem::path &absolute_path) const {
        const std::string *best_scheme = nullptr;
        std::string best_relative;
        for (const auto &[scheme, mount] : m_mounts) {
            if (!IsPathInside(mount.root, absolute_path)) {
                continue;
            }
            const auto relative = std::filesystem::relative(absolute_path, mount.root).generic_string();
            if (best_scheme == nullptr || relative.size() < best_relative.size()) {
                best_scheme = &scheme;
                best_relative = relative;
            }
        }
        if (best_scheme == nullptr) {
            throw std::runtime_error("No mount contains path: " + absolute_path.generic_string());
        }
        return AssetPath(*best_scheme, best_relative);
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

    std::optional<GUID> FileSystemDatabase::GetGUID(const AssetPath &path) const {
        const auto it = m_path_to_guid.find(path);
        if (it == m_path_to_guid.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void FileSystemDatabase::AddAsset(const GUID &guid, const AssetPath &path) {
        if (m_assets_map.find(guid) != m_assets_map.end()) throw std::runtime_error("asset GUID already exists");
        m_assets_map.emplace(guid, path);
        m_path_to_guid[path] = guid;
    }

    void FileSystemDatabase::SaveArchive(AnnoRefl::Archive &archive, GUID guid) {
        SaveArchive(archive, GetAssetPath(guid));
    }

    void FileSystemDatabase::LoadArchive(AnnoRefl::Archive &archive, GUID guid) {
        LoadArchive(archive, GetAssetPath(guid));
    }

    void FileSystemDatabase::SaveArchive(AnnoRefl::Archive &archive, const AssetPath &path) {
        GUID guid;
        if (!::GetGUID(archive, guid)) {
            throw std::runtime_error("Failed to get GUID from asset file: " + path.generic_string());
        }
        if (m_assets_map.find(guid) == m_assets_map.end()) {
            AddAsset(guid, path);
        }
        if (!IsWritableScheme(path)) {
            throw std::runtime_error("Cannot save asset to read-only scheme: " + std::string(path.GetScheme()));
        }
        auto json_path = ToAbsolutePath(path);
        EnsurePathExists(json_path);
        archive.save_to_file(json_path.replace_extension(""));
    }

    void FileSystemDatabase::LoadArchive(AnnoRefl::Archive &archive, const AssetPath &path) {
        auto json_path = ToAbsolutePath(path);
        archive.load_from_file(json_path.replace_extension(""));
    }

    std::vector<FileSystemDatabase::AssetInfo> FileSystemDatabase::ListDirectory(const AssetPath &path) const {
        std::vector<AssetInfo> assets;
        for (auto &entry : std::filesystem::directory_iterator(ToAbsolutePath(path))) {
            AssetInfo info{};
            info.path = FromAbsolutePath(entry.path());
            if (entry.is_directory()) {
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
        if (!IsWritableScheme(path)) {
            return false;
        }
        std::error_code ec;
        const auto absolute_path = ToAbsolutePath(path);
        std::filesystem::create_directories(absolute_path, ec);
        if (ec) {
            return false;
        }
        return std::filesystem::exists(absolute_path);
    }

    bool FileSystemDatabase::MovePath(const AssetPath &from, const AssetPath &to) {
        if (!IsWritableScheme(from) || !IsWritableScheme(to)) {
            return false;
        }

        const auto from_abs = ToAbsolutePath(from);
        const auto to_abs = ToAbsolutePath(to);
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
            const auto old_abs = ToAbsolutePath(path);
            if (IsPathInside(from_abs, old_abs)) {
                const auto rel = std::filesystem::relative(old_abs, from_abs, ec);
                if (ec) {
                    continue;
                }
                remapped_assets.emplace_back(guid, to / rel.generic_string());
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
        if (!IsWritableScheme(path)) {
            return false;
        }

        const auto abs_path = ToAbsolutePath(path);
        std::error_code ec;
        if (!std::filesystem::exists(abs_path, ec)) {
            return false;
        }

        std::vector<GUID> to_remove;
        if (std::filesystem::is_directory(abs_path, ec)) {
            for (const auto &[guid, asset_path] : m_assets_map) {
                if (IsPathInside(abs_path, ToAbsolutePath(asset_path))) {
                    to_remove.push_back(guid);
                }
            }
            std::filesystem::remove_all(abs_path, ec);
        } else {
            if (abs_path.extension() == k_asset_file_extension) {
                GUID guid;
                if (::GetGUID(abs_path, guid)) {
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

    std::filesystem::path FileSystemDatabase::GetProjectAssetsPath() const {
        const auto it = m_mounts.find(std::string(AssetPath::k_scheme_res));
        return it == m_mounts.end() ? std::filesystem::path{} : it->second.root;
    }

    void FileSystemDatabase::LoadBuiltinAssets(const std::filesystem::path &path) {
        RegisterScheme(AssetPath::k_scheme_builtin, path, false);
        for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(path)) {
            const std::filesystem::path relative_path = std::filesystem::relative(entry.path(), path);
            if (relative_path.extension() == k_asset_file_extension) {
                std::ifstream file(entry.path());
                if (file.is_open()) {
                    GUID guid;
                    if (::GetGUID(entry.path(), guid)) {
                        AddAsset(guid, AssetPath(AssetPath::k_scheme_builtin, relative_path.generic_string()));
                    }
                    file.close();
                }
            }
        }
    }

    void FileSystemDatabase::LoadProjectAssets(const std::filesystem::path &path) {
        RegisterScheme(AssetPath::k_scheme_res, path);
        for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(path)) {
            const std::filesystem::path relative_path = std::filesystem::relative(entry.path(), path);
            if (relative_path.extension() == k_asset_file_extension) {
                std::ifstream file(entry.path());
                if (file.is_open()) {
                    GUID guid;
                    if (::GetGUID(entry.path(), guid)) {
                        AddAsset(guid, AssetPath(AssetPath::k_scheme_res, relative_path.generic_string()));
                    }
                    file.close();
                }
            }
        }
    }
} // namespace Engine
