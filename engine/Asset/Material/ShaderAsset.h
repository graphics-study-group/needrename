#ifndef ASSET_MATERIAL_SHADERASSET_INCLUDED
#define ASSET_MATERIAL_SHADERASSET_INCLUDED

#include <Asset/Asset.h>
#include <Reflection/macros.h>

namespace Engine {

    struct REFL_SER_CLASS(REFL_WHITELIST) ShaderInBlockVariableProperty 
    {
        REFL_SER_BODY(ShaderInBlockVariableProperty)

        REFL_ENABLE ShaderInBlockVariableProperty() = default;
        virtual ~ShaderInBlockVariableProperty() = default;

        /// @brief what is the type of this variable?
        enum class InBlockVarType {
            Undefined,
            Float,
            Int,
            Vec4,
            Mat4
        };

        enum class Frequency {
            PerScene = 0,
            PerCamera = 1,
            PerMaterial = 2,
            PerModel = 3
        };

        static constexpr size_t SizeOf(InBlockVarType type) {
            switch(type) {
            case InBlockVarType::Float:
            case InBlockVarType::Int:
                return 4;
            case InBlockVarType::Vec4:
                return 16;
            case InBlockVarType::Mat4:
                return 64;
            default:
                return 0;
            }
        };

        /// @brief what is the type of this variable?
        REFL_SER_ENABLE InBlockVarType type {};

        /// @brief How frequently the value this variable changes?
        /// Corresponds to descriptor set index of this variable.
        REFL_SER_ENABLE Frequency frequency {};

        /// @brief the binding of the UBO that this variable belongs to.
        /// Defaults to 0.
        REFL_SER_ENABLE uint32_t binding {};
        
        /// @brief Offset of this uniform variable in UBO.
        /// Ignored for non-UBO variables such as textures.
        REFL_SER_ENABLE uint32_t offset {};

        REFL_SER_ENABLE std::string name {};
    };

    /// @brief Stores information of each uniform varible in a given shader file.
    /// Ideally it should be provided by a SPIR-V reflection library.
    /// We manually store them in MaterialTemplateAsset for now.
    struct REFL_SER_CLASS(REFL_WHITELIST) ShaderVariableProperty
    {
        REFL_SER_BODY(ShaderVariableProperty)

        REFL_ENABLE ShaderVariableProperty() = default;
        virtual ~ShaderVariableProperty() = default;

        using Frequency = ShaderInBlockVariableProperty::Frequency;

        /// @brief what is the type of this variable?
        REFL_SER_ENABLE enum class Type {
            Undefined,
            // Uniform buffer object, i.e. a buffer for bulk uniform variables.
            UBO,
            // Shader storage buffer object.
            StorageBuffer,
            // Texture to be sampled (or combined image sampler).
            Texture,
            // Storage image. Generally used in compute shaders.
            StorageImage,
        } type {};

        /// @brief How frequently the value this variable changes?
        /// Corresponds to descriptor set index of this variable.
        REFL_SER_ENABLE Frequency frequency{};

        /// @brief Binding of this uniform, should be 0 for uniform variables in UBOs.
        REFL_SER_ENABLE uint32_t binding {};

        /// @brief Name of this uniform, which can be used as an alternative of binding or offset
        REFL_SER_ENABLE std::string name {};
    };

    class REFL_SER_CLASS(REFL_WHITELIST) ShaderAsset : public Asset {
        REFL_SER_BODY(ShaderAsset)
    public:
        REFL_ENABLE ShaderAsset () = default;

        REFL_SER_ENABLE enum class ShaderType {
            None,
            Vertex,
            Fragment,
            Compute,
            TessellationControl,
            TessellationEvaluation,
            Geometry
        } shaderType {ShaderType::None};

        /// @brief Name of the shader.
        REFL_SER_ENABLE std::string m_name {};
        /// @brief Entry point name for the shader, case-sensitive. Default to "main".
        REFL_SER_ENABLE std::string m_entry_point {};
        std::vector <uint32_t> binary {};

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;
    };
}

#endif // ASSET_MATERIAL_SHADERASSET_INCLUDED
