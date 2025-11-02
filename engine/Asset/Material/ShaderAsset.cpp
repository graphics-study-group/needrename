#include "ShaderAsset.h"
#include <Reflection/serialization.h>
#include <fstream>
#include <cstdio>
namespace Engine {
    void ShaderAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        auto &json = *archive.m_cursor;
        size_t extra_data_id = archive.create_new_extra_data_buffer(".mesh");
        json["%extra_data_id"] = extra_data_id;
        auto &data = archive.m_context->extra_data[extra_data_id];
        size_t file_size = binary.size() * sizeof(uint32_t);
        data.resize(file_size);
        std::memcpy(data.data(), binary.data(), file_size);

        Asset::save_asset_to_archive(archive);
    }

    void ShaderAsset::load_asset_from_archive(Serialization::Archive &archive) {
        auto &json = *archive.m_cursor;
        auto &data = archive.m_context->extra_data[json["%extra_data_id"].get<size_t>()];
        size_t file_size = data.size();
        binary.resize((file_size - sizeof(uint32_t) + 1) / sizeof(uint32_t) + 1);
        std::memcpy(binary.data(), data.data(), file_size);

        Asset::load_asset_from_archive(archive);
    }

    void ShaderAsset::LoadFromFile(
        const std::filesystem::path &path,
        ShaderType type,
        const std::string &name,
        const std::string &entry_point
    ) {
        shaderType = type;

        m_name = name.empty() ? path.stem().string() : name;
        m_entry_point = entry_point;

        // Load binary data
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        assert(file.is_open());
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        printf("Loading shader %s, size %zu bytes\n", path.string().c_str(), file_size);
        binary.resize((file_size - sizeof(uint32_t) + 1) / sizeof(uint32_t) + 1);
        file.read(reinterpret_cast<char *>(binary.data()), file_size);
        file.close();
    }
} // namespace Engine
