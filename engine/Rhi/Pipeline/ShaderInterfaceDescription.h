#ifndef ENGINE_RHI_SHADERINTERFACEDESCRIPTION_INCLUDED
#define ENGINE_RHI_SHADERINTERFACEDESCRIPTION_INCLUDED

#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace Engine::Rhi {
    /**
     * @brief One descriptor binding a shader declares.
     *
     * The bake records these; a runtime that receives baked code reads them instead of
     * reflecting. `kind` is the string the baker wrote, kept as a string so that a
     * description from a newer bake does not silently become a different type - a caller
     * that does not recognise a kind sees it.
     */
    struct RHI_API ShaderInterfaceBinding {
        /// @brief Descriptor set the binding belongs to.
        uint32_t set{0};
        /// @brief Binding number within the set.
        uint32_t binding{0};
        /// @brief What the binding accepts, as the baker named it.
        std::string kind{};
        /// @brief The name the shader declares it with.
        std::string name{};
        /// @brief For a storage image, the format it is declared with, as `ImageFormat`'s value.
        uint32_t storage_format{0};
        /// @brief For a storage image, whether the shader only reads it.
        bool storage_read_only{false};
        /// @brief For a sampled image, what a texel holds, as `TextureSampleType`'s value.
        uint32_t sample_type{0};
        /// @brief For a sampled image, how the binding is declared, as `TextureViewDimension`'s value.
        uint32_t view_dimension{1};
        /// @brief One variable of a buffer binding's block, as the shader declares it.
        ///
        /// A material places a uniform variable by name into its block, and the offset it needs
        /// for that is the one the shader's layout states. A runtime that reflects the module reads
        /// it from the module; one that does not reads it from here.
        struct Member {
            /// @brief Name of the variable.
            std::string name{};
            /// @brief Offset of the variable inside the block.
            uint32_t offset{0};
            /// @brief Size of the variable in bytes.
            uint32_t size{0};
            /// @brief What kind of variable it is, as `StructuredBufferPlacer::Kind`.
            ///
            /// A runtime that rebuilds the block from this description has no C++ type for the
            /// variable, and the placer checks one; the kind is the portable spelling it maps back.
            uint32_t kind{0};
        };

        /// @brief The variables of a buffer binding, outermost block first.
        std::vector<Member> members{};
        /// @brief The declared size of a buffer binding's block, trailing padding included.
        uint32_t block_size{0};

        /// @brief For a combined binding, the slot its sampler occupies on a splitting backend.
        ///
        /// `UINT32_MAX` means the bake recorded no split, which is what a backend that accepts a
        /// combined binding reads as "not applicable".
        uint32_t sampler_binding{std::numeric_limits<uint32_t>::max()};
    };

    /**
     * @brief Everything a runtime needs to bind a baked shader without reflecting it.
     *
     * This is the seam's counterpart to `ShaderInterface`, which is the output of runtime
     * reflection and therefore unavailable to a backend that cannot reflect. A backend that
     * gets its code from the bake reads this instead.
     *
     * The two are produced from the same facts: the Vulkan backend can build one of these
     * from its `ShaderInterface`, and the bake writes one from the same reflection. That is
     * what makes it a seam type rather than a WebGPU type.
     */
    struct RHI_API ShaderInterfaceDescription {
        /// @brief The shader's logical name.
        std::string shader{};
        /// @brief The stage it runs as, as the baker named it.
        std::string stage{};
        /// @brief The function the pipeline starts at.
        std::string entry_point{"main"};
        /// @brief Where the constant block sits, when the shader has one.
        struct ConstantBlock {
            uint32_t set{0};
            uint32_t binding{0};
            uint32_t size{0};
        } constant_block{};
        /// @brief Whether the shader declares a constant block at all.
        bool has_constant_block{false};
        /// @brief Every binding the shader declares.
        std::vector<ShaderInterfaceBinding> bindings{};

        /**
         * @brief Read a description written by the shader baker.
         *
         * @param path The `.interface.json` file next to a baked module.
         * @return The description, or nothing when the file cannot be read or parsed; the
         * caller reports which file it was.
         */
        static std::optional<ShaderInterfaceDescription> FromFile(const std::filesystem::path &path);

        /**
         * @brief Read a description from the JSON text an archive carries.
         *
         * A baked shader asset stores its description as text rather than as a file of its own,
         * so a runtime that only has the asset still gets the binding table.
         *
         * @param text The JSON text.
         * @return The description, or nothing when the text cannot be parsed.
         */
        static std::optional<ShaderInterfaceDescription> FromString(const std::string &text);

        /// @brief The descriptor type a `kind` string names, or a value with `UNDEFINED`
        /// placeholder when it is not recognised.
        static DescriptorType ToDescriptorType(const std::string &kind);
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_SHADERINTERFACEDESCRIPTION_INCLUDED
