#ifndef ASSET_MATERIAL_MATERIALASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALASSET_INCLUDED

#include <memory>
#include <string>
#include <glm.hpp>
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <Asset/Asset.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class TextureAsset;

    class REFL_SER_CLASS(REFL_WHITELIST) MaterialAsset : public Asset
    {
        REFL_SER_BODY()
    public:
        REFL_ENABLE MaterialAsset() = default;
        virtual ~MaterialAsset() = default;

        REFL_SER_ENABLE std::string m_name{};
        REFL_SER_ENABLE std::unordered_map<std::string, float> m_floats{};
        REFL_SER_ENABLE std::unordered_map<std::string, int> m_ints{};
        REFL_SER_ENABLE std::unordered_map<std::string, glm::vec4> m_vec4s{};
        REFL_SER_ENABLE std::unordered_map<std::string, glm::mat4> m_mat4s{};
        REFL_SER_ENABLE std::unordered_map<std::string, std::shared_ptr<TextureAsset>> m_textures{};

        void LoadFromTinyObj(const tinyobj::material_t &material, const std::filesystem::path &base_path = "");
    };
}

#endif // ASSET_MATERIAL_MATERIALASSET_INCLUDED
