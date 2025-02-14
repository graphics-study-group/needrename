#include "ShaderAsset.h"

namespace Engine
{
    void ShaderAsset::save_asset_to_archive(Serialization::Archive &archive) const
    {
        auto &data = archive.m_context->extra_data;
        assert(data.empty());
        size_t file_size = binary.size() * sizeof(uint32_t);
        data.resize(file_size);
        std::memcpy(data.data(), binary.data(), file_size);

        Asset::save_asset_to_archive(archive);
    }

    void ShaderAsset::load_asset_from_archive(Serialization::Archive &archive)
    {
        auto &data = archive.m_context->extra_data;
        size_t file_size = data.size();
        binary.resize((file_size - sizeof(uint32_t) + 1) / sizeof(uint32_t) + 1);
        std::memcpy(binary.data(), data.data(), file_size);

        Asset::load_asset_from_archive(archive);
    }
}
