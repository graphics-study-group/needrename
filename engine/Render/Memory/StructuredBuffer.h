#ifndef RENDER_MEMORY_STRUCTUREDBUFFER_INCLUDED
#define RENDER_MEMORY_STRUCTUREDBUFFER_INCLUDED

#include <memory>
#include <span>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace Engine {

    /**
     * @brief A structured buffer that every contained variable is named.
     *
     * It is implemented via a mapping from name to bitwise content of the
     * variable. To actually write these variables onto a buffer, use
     * `Engine::StructuredBufferPlacer`
     */
    class StructuredBuffer {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        /// @brief An entry for a variable in the structured buffer
        struct VariableEntry {
            /// RTTI information of the type.
            const std::type_info *type{nullptr};
            /// Size of the variable in bytes.
            size_t size{0};

            /// Actual byte content of the variable.
            /// @todo Maybe we can use small vector or memory pool here.
            std::vector<std::byte> value{0};
        };

        StructuredBuffer();
        ~StructuredBuffer() noexcept;

        /**
         * @brief Set the variable of name to a value.
         *
         * @param name Name of the variable.
         * @param ptr Pointer to the data containing the value. Caller must ensure
         * that the pointer can be casted into a variable of the given type.
         */
        void SetVariable(const std::string &name, const void *ptr, size_t size, const std::type_info &type) noexcept;

        /**
         * @brief Set the variable of name to a value.
         *
         * Safer alternative to the `void *` version.
         */
        template <typename T>
        void SetVariable(const std::string &name, std::type_identity<T>::type val) noexcept
            requires(
                std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>
                && !std::is_array_v<T>
            )
        {
            using ActualType = std::remove_cvref_t<T>;
            SetVariable(name, &val, sizeof(ActualType), typeid(ActualType));
        }

        /**
         * @brief Set the variable of name to a value by an array.
         *
         * Safer alternative to the `void *` version.
         */
        template <typename T, size_t extent>
        void SetVariable(const std::string &name, std::type_identity<std::span<T, extent>>::type spn) noexcept
            requires(
                std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>
                && !std::is_array_v<T> && extent != std::dynamic_extent
            )
        {
            static_assert(extent == spn.size());
            using ActualType = std::remove_cvref_t<T>;
            SetVariable(name, spn.data(), sizeof(ActualType) * extent, typeid(ActualType[extent]));
        }

        /**
         * @brief Set the structured buffer variable.
         *
         * The caller must ensure that the specified buffer is available when writing it.
         */
        void SetStructuredBuffer(const std::string &name, const StructuredBuffer &buffer);

        const VariableEntry *GetVariable(const std::string &name) const;
    };
} // namespace Engine

#endif // RENDER_MEMORY_STRUCTUREDBUFFER_INCLUDED
