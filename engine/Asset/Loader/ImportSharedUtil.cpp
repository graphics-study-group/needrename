#include "ImportSharedUtil.h"

#include <Asset/Asset.h>
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Reflection/Archive.h>

#include <algorithm>
#include <cctype>

namespace Engine::detail::import_shared {
    std::string SanitizeAssetName(std::string name) {
        if (name.empty()) {
            return "unnamed";
        }
        std::replace_if(
            name.begin(),
            name.end(),
            [](unsigned char ch) {
                switch (ch) {
                case '<':
                case '>':
                case ':':
                case '"':
                case '/':
                case '\\':
                case '|':
                case '?':
                case '*':
                    return true;
                default:
                    return std::iscntrl(ch) != 0;
                }
            },
            '_'
        );
        return name;
    }

    std::string MakeUniqueAssetName(
        const std::string &base_name, std::unordered_map<std::string, uint32_t> &name_counters
    ) {
        std::string sanitized = SanitizeAssetName(base_name);
        auto [it, inserted] = name_counters.try_emplace(sanitized, 0);
        if (inserted) {
            return sanitized;
        }
        it->second += 1;
        return sanitized + "_" + std::to_string(it->second);
    }

    void AppendVertexAttribute(
        std::vector<std::byte> &buffer, const float *data, size_t float_count, MeshAsset::Submesh::Attributes &attr
    ) {
        attr.buffer_offset = buffer.size();
        const auto *begin = reinterpret_cast<const std::byte *>(data);
        const auto *end = reinterpret_cast<const std::byte *>(data + float_count);
        buffer.insert(buffer.end(), begin, end);
        attr.buffer_size = float_count * sizeof(float);
    }

    AssetPath MakeAssetPath(
        FileSystemDatabase &database, const std::filesystem::path &path_in_project, const std::string &asset_name
    ) {
        return AssetPath(database, path_in_project / (asset_name + ".asset"));
    }

    void SaveAsset(
        FileSystemDatabase &database,
        const Asset &asset,
        const std::filesystem::path &path_in_project,
        const std::string &asset_name
    ) {
        Serialization::Archive archive;
        archive.prepare_save();
        asset.save_asset_to_archive(archive);
        database.SaveArchive(archive, MakeAssetPath(database, path_in_project, asset_name));
    }
} // namespace Engine::detail::import_shared
