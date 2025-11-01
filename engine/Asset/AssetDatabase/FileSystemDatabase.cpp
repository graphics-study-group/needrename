#include "FileSystemDatabase.h"
#include <Reflection/Archive.h>
#include <fstream>
#include <nlohmann/json.hpp>

static bool GetGUID(const std::filesystem::path &asset_path, Engine::GUID &out_guid) {
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

namespace Engine {
    FileSystemDatabase::IterImpl::IterImpl(std::filesystem::path root, const std::string &prefix, bool recursive) :
        m_root(root), m_prefix(prefix), m_recursive(recursive) {
        if (recursive) m_rit = std::make_unique<std::filesystem::recursive_directory_iterator>(root);
        else m_it = std::make_unique<std::filesystem::directory_iterator>(root);
    }

    std::unique_ptr<AssetDatabase::ListRange::IterImplBase> FileSystemDatabase::IterImpl::begin_clone() const {
        return std::make_unique<IterImpl>(m_root, m_prefix, m_recursive);
    }

    bool FileSystemDatabase::IterImpl::next(AssetPair &out) {
        GUID guid;
        while (true) {
            if (m_recursive) {
                if (!m_rit || *m_rit == std::filesystem::end(*m_rit)) return false;
                const auto &entry = **m_rit;
                auto rel = std::filesystem::relative(entry.path(), m_root);
                if (rel.extension() == k_asset_file_extension && GetGUID(entry.path(), guid)) {
                    out = {m_prefix / rel, guid};
                    ++(*m_rit);
                    return true;
                } else {
                    ++(*m_rit);
                }
            } else {
                if (!m_it || *m_it == std::filesystem::end(*m_it)) return false;
                const auto &entry = **m_it;
                auto rel = std::filesystem::relative(entry.path(), m_root);
                if (rel.extension() == k_asset_file_extension && GetGUID(entry.path(), guid)) {
                    out = {m_prefix / rel, guid};
                    ++(*m_it);
                    return true;
                } else {
                    ++(*m_it);
                }
            }
        }
    }

    void FileSystemDatabase::SaveArchive(Serialization::Archive &archive, std::filesystem::path path) {
        auto json_path = ProjectPathToFilesystemPath(path);
        std::ofstream json_file(json_path);
        if (json_file.is_open()) {
            json_file << archive.m_context->json.dump(4) << std::endl;
            json_file.close();
        } else {
            throw std::runtime_error("Failed to open json file");
        }

        auto extra_data_path = json_path.replace_extension("");
        if (archive.m_context->extra_data.size() > 0) {
            std::ofstream extra_data_file(extra_data_path, std::ios::binary);
            if (extra_data_file.is_open()) {
                extra_data_file.write(
                    reinterpret_cast<const char *>(archive.m_context->extra_data.data()),
                    archive.m_context->extra_data.size()
                );
                extra_data_file.close();
            } else {
                throw std::runtime_error("Failed to open extra data file");
            }
        }
    }

    void FileSystemDatabase::LoadArchive(Serialization::Archive &archive, std::filesystem::path path) {
        auto json_path = ProjectPathToFilesystemPath(path);
        archive.clear();
        std::ifstream json_file(json_path);
        if (json_file.is_open()) {
            json_file >> archive.m_context->json;
            json_file.close();
        } else {
            throw std::runtime_error("Failed to open .asset file");
        }

        auto extra_data_path = json_path.replace_extension("");
        std::ifstream extra_data_file(extra_data_path, std::ios::binary);
        if (extra_data_file.is_open()) {
            extra_data_file.seekg(0, std::ios::end);
            size_t size = extra_data_file.tellg();
            extra_data_file.seekg(0, std::ios::beg);
            archive.m_context->extra_data.resize(size);
            extra_data_file.read(reinterpret_cast<char *>(archive.m_context->extra_data.data()), size);
            extra_data_file.close();
        }
    }

    AssetDatabase::ListRange FileSystemDatabase::ListAssets(std::filesystem::path directory, bool recursive) {
        auto root = ProjectPathToFilesystemPath(directory);
        std::string prefix = directory.string().starts_with("~") ? "~" : "/";
        return ListRange(std::make_unique<IterImpl>(root, prefix, recursive));
    }

    std::filesystem::path FileSystemDatabase::GetAssetsDirectory() const {
        return m_project_asset_path;
    }

    void FileSystemDatabase::SetBuiltinAssetPath(const std::filesystem::path &path) {
        m_builtin_asset_path = path.generic_string();
    }

    void FileSystemDatabase::SetProjectAssetPath(const std::filesystem::path &path) {
        m_project_asset_path = path.generic_string();
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
