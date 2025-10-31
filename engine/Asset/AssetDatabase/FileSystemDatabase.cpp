#include "FileSystemDatabase.h"
#include <Reflection/Archive.h>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Engine {
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

    std::vector<std::pair<std::filesystem::path, GUID>> FileSystemDatabase::ListAssets(
        std::filesystem::path directory, bool recursive
    ) {
        auto root = ProjectPathToFilesystemPath(directory);
        std::vector<std::pair<std::filesystem::path, GUID>> result;
        std::filesystem::path prefix = directory.string().starts_with("~") ? "~" : "/";
        GUID guid;
        if (recursive == false) {
            for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(root)) {
                std::filesystem::path relative_path = std::filesystem::relative(entry.path(), root);
                if (relative_path.extension() == k_asset_file_extension && GetGUID(entry.path(), guid)) {
                    result.push_back({prefix / relative_path, guid});
                }
            }
        } else {
            for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(root)) {
                std::filesystem::path relative_path = std::filesystem::relative(entry.path(), root);
                if (relative_path.extension() == k_asset_file_extension && GetGUID(entry.path(), guid)) {
                    result.push_back({prefix / relative_path, guid});
                }
            }
        }
        return result;
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

    bool FileSystemDatabase::GetGUID(const std::filesystem::path &asset_path, GUID &out_guid) const {
        std::ifstream file(asset_path);
        if (file.is_open()) {
            nlohmann::json json_data = nlohmann::json::parse(file);
            if (json_data.contains("%main_id")) {
                std::string str_id = json_data["%main_id"].get<std::string>();
                if (json_data["%data"].contains(str_id) && json_data["%data"][str_id].contains("Asset::m_guid")) {
                    out_guid = GUID(json_data["%data"][str_id]["Asset::m_guid"].get<std::string>());
                    file.close();
                    return true;
                }
            }
            file.close();
        }
        return false;
    }
} // namespace Engine
