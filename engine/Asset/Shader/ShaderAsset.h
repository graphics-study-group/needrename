#ifndef ASSET_MATERIAL_SHADERASSET_INCLUDED
#define ASSET_MATERIAL_SHADERASSET_INCLUDED

#include <Asset/Asset.h>
#include <Reflection/macros.h>
#include <string>
#include <vector>

namespace Engine {
    /**
     * @brief An asset that refers to a shader.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) ShaderAsset : public Asset {
        REFL_SER_BODY(ShaderAsset)
    public:
        REFL_ENABLE ShaderAsset() = default;

        /**
         * @brief Type of the shader.
         */
        REFL_SER_ENABLE enum class REFL_SER_CLASS() ShaderType {
            None,
            Vertex,
            Fragment,
            Compute,
            TessellationControl,
            TessellationEvaluation,
            Geometry
        } shaderType{ShaderType::None};

        REFL_SER_ENABLE enum class REFL_SER_CLASS() StoreType {
            GLSL,
            SPIRV
        } storeType{StoreType::SPIRV};

        /// @brief Name of the shader.
        REFL_SER_ENABLE std::string m_name{};
        /// @brief Entry point name for the shader, case-sensitive. Default to "main".
        REFL_SER_ENABLE std::string m_entry_point{};

        std::vector<uint32_t> binary{};
        std::string glsl_code{};

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

        /**
         * @brief Load a shader from a disk file.
         * @note For debug proposes only.
         */
        void LoadFromFile(
            const std::filesystem::path &path,
            ShaderType type,
            const std::string &name = "",
            const std::string &entry_point = "main"
        );

        /**
         * @brief Compile a shader via the compiled provided by the MainClass.
         */
        bool Compile(std::filesystem::path shader_path_abs = std::filesystem::path{});
    };
} // namespace Engine

#endif // ASSET_MATERIAL_SHADERASSET_INCLUDED
