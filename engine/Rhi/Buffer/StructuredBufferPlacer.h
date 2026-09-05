#ifndef ENGINE_RHI_STRUCTUREDBUFFERPLACER_INCLUDED
#define ENGINE_RHI_STRUCTUREDBUFFERPLACER_INCLUDED

#include "Rhi/rhi_export.h"

#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace Engine::Rhi {

    class StructuredBuffer;

    /**
     * @brief A helper that places named variables into a structured buffer.
     */
    class RHI_API StructuredBufferPlacer {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        StructuredBufferPlacer() noexcept;
        ~StructuredBufferPlacer() noexcept;

        /**
         * @brief Add a name-type entry to the structure mapping.
         * Overrides previous type info of the same name.
         *
         * @param name name of the variable.
         * @param offset offset of the variable into the buffer.
         * @param size size of the variable.
         * @param type RTTI info for the type. Pass a `nullptr` to skip RTTI checks on this variable.
         */
        void AddVariable(const std::string &name, size_t offset, size_t size, const std::type_info *type);

        template <typename T>
        void AddVariable(const std::string &name, size_t offset)
            requires(std::is_standard_layout_v<T>)
        {
            // typeid returns static life-time variables, so taking is address should be fine.
            AddVariable(name, offset, sizeof(T), &typeid(T));
        };

        /**
         * @brief Mark a variable as a structured buffer.
         * Use this to place another structured buffer into it.
         *
         * @param buffer The subbuffer. It is up to the caller to ensure:
         *          - Subbuffer outlives the main buffer;
         *          - No cycle exists in the chain of subbuffers.
         */
        void AddStructuredBuffer(const std::string &name, size_t offset, const StructuredBufferPlacer &buffer);

        /**
         * @brief Get the buffer size required to hold every member write.
         *
         * Equals the end of the last member (`max(offset + size)`), recursively
         * accounting for nested structured buffers. Trailing padding is not
         * included. `WriteBuffer` resizes its staging vector to this size.
         *
         * @return The required size in bytes.
         */
        size_t GetRequiredSize() const noexcept;

        /**
         * @brief Declare the total block size in the target layout.
         *
         * The target layout's size in bytes, including trailing padding. Required
         * when the placer mirrors a layout that pads the buffer to an alignment
         * boundary: pass the target struct's `sizeof` for CPU mirror structs, or
         * the std140-rounded block size for shader-reflected uniform buffers. Call
         * after all members are added. The value must be at least `GetRequiredSize()`.
         *
         * @param block_size The block size in bytes, including trailing padding.
         */
        void SetBlockSize(size_t block_size) noexcept;

        /**
         * @brief Get the total block size of the buffer.
         *
         * Returns the value passed to `SetBlockSize`, or falls back to
         * `GetRequiredSize()` when no block size was declared. Debug builds assert
         * that a declared block size is not smaller than the required size.
         *
         * @return The block size in bytes.
         */
        size_t GetBlockSize() const noexcept;

        /**
         * @brief Write the structured buffer into memory.
         */
        void WriteBuffer(const StructuredBuffer &data, std::byte *buffer) const noexcept;
        void WriteBuffer(const StructuredBuffer &data, std::vector<std::byte> &buffer) const;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_STRUCTUREDBUFFERPLACER_INCLUDED
