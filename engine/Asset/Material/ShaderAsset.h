#ifndef ASSET_MATERIAL_SHADERASSET_INCLUDED
#define ASSET_MATERIAL_SHADERASSET_INCLUDED

#include <Asset/Asset.h>
#include <Reflection/macros.h>

namespace Engine {
    class REFL_SER_CLASS(REFL_WHITELIST) ShaderAsset : public Asset {
        REFL_SER_BODY(ShaderAsset)
    public:
        REFL_ENABLE ShaderAsset() = default;

        REFL_SER_ENABLE enum class REFL_SER_CLASS() ShaderType {
            None,
            Vertex,
            Fragment,
            Compute,
            TessellationControl,
            TessellationEvaluation,
            Geometry
        } shaderType{ShaderType::None};

        /// @brief Name of the shader.
        REFL_SER_ENABLE std::string m_name{};
        /// @brief Entry point name for the shader, case-sensitive. Default to "main".
        REFL_SER_ENABLE std::string m_entry_point{};
        std::vector<uint32_t> binary{};

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;
        void LoadFromFile(
            const std::filesystem::path &path,
            ShaderType type,
            const std::string &name = "",
            const std::string &entry_point = "main"
        );
    };
} // namespace Engine

#endif // ASSET_MATERIAL_SHADERASSET_INCLUDED
