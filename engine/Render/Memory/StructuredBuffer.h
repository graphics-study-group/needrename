#ifndef RENDER_MEMORY_STRUCTUREDBUFFER_INCLUDED
#define RENDER_MEMORY_STRUCTUREDBUFFER_INCLUDED

#include <span>
#include <memory>
#include <vector>
#include <typeinfo>
#include <type_traits>

namespace Engine {
    class StructuredBuffer {
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:
        struct VariableEntry {
            const std::type_info * type {nullptr};
            size_t size {0};

            // Maybe we can use small vector or memory pool here.
            std::vector <std::byte> value {0};
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
        void SetVariable(
            const std::string & name,
            const void * ptr,
            size_t size,
            const std::type_info & type
        ) noexcept;

        /**
         * @brief Set the variable of name to a value.
         * 
         * Safer alternative to the `void *` version.
         */
        template <typename T>
        void SetVariable(
            const std::string & name,
            T val
        ) noexcept requires (
            std::is_standard_layout_v<T> &&
            std::is_trivially_copyable_v<T> &&
            !std::is_pointer_v<T> &&
            !std::is_array_v<T>
        ) {
            using ActualType = std::remove_cvref_t<T>;
            SetVariable(name, &val, sizeof(ActualType), typeid(ActualType));
        }

        template <typename T, size_t extent>
        void SetVariable(
            const std::string & name,
            std::span<T, extent> spn
        ) noexcept requires (
            std::is_standard_layout_v<T> &&
            std::is_trivially_copyable_v<T> &&
            !std::is_pointer_v<T> &&
            !std::is_array_v<T> &&
            extent != std::dynamic_extent
        ) {
            static_assert(extent == spn.size());
            using ActualType = std::remove_cvref_t<T>;
            SetVariable(name, spn.data(), sizeof(ActualType) * extent, typeid(ActualType[extent]));
        }

        /**
         * @brief Set the structured buffer variable.
         * 
         * The caller must ensure that the specified buffer is available when writing it.
         */
        void SetStructuredBuffer (
            const std::string & name,
            const StructuredBuffer & buffer
        );

        const VariableEntry * GetVariable(
            const std::string & name
        ) const;
    };
}

#endif // RENDER_MEMORY_STRUCTUREDBUFFER_INCLUDED
