#ifndef RENDER_MEMORY_STRUCTUREDBUFFER_INCLUDED
#define RENDER_MEMORY_STRUCTUREDBUFFER_INCLUDED

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

        template <typename T>
        void SetVariable(
            const std::string & name,
            T val
        ) noexcept requires (
            std::is_standard_layout_v<T> &&
            std::is_trivially_copyable_v<T> &&
            !std::is_pointer_v<T> &&
            std::is_array_v<T> && 
            std::rank_v<T> == 1 &&
            std::is_bounded_array_v<T>
        ) {
            constexpr auto ext = std::extent_v<T, 0>;
            using ActualElemType = std::remove_cv_t<std::remove_extent_t<T>>;

            SetVariable(name, val, sizeof(ActualElemType) * ext, typeid(ActualElemType[ext]));
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
