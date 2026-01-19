#ifndef RENDER_MEMORY_STRUCTUREDBUFFERPLACER_INCLUDED
#define RENDER_MEMORY_STRUCTUREDBUFFERPLACER_INCLUDED

#include <memory>
#include <vector>
#include <type_traits>
#include <typeinfo>

namespace Engine {
    // Opaque class holding a structured buffer
    class StructuredBuffer;

    /**
     * @brief A helper that places named variables into a structured buffer.
     */
    class StructuredBufferPlacer {
        struct impl;
        std::unique_ptr <impl> pimpl;

        bool TypeCheck (const std::string & name, const std::type_info & type) const noexcept;

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
        void AddVariable(
            const std::string & name,
            size_t offset,
            size_t size,
            const std::type_info * type
        );

        template <typename T>
        void AddVariable(
            const std::string & name,
            size_t offset
        ) requires (std::is_standard_layout_v<T>) {
            // typeid returns static life-time variables, so taking is address should be fine.
            AddVariable(name, offset, sizeof(T), &typeid(T));
        };

        /**
         * @brief Create a new structured buffer by this type.
         */
        std::unique_ptr <StructuredBuffer> CreateBuffer() const noexcept;

        /**
         * @brief Set the variable of name to a value.
         * 
         * @param buf the structured buffer.
         * @param name Name of the variable. If no variable of the name is found,
         * then no change to the state of this class will be effectuated.
         * @param ptr Pointer to the data containing the value. Caller must ensure
         * that the pointer can be casted into a variable of the given type.
         */
        void SetVariable(
            StructuredBuffer & buf,
            const std::string & name,
            const void * ptr
        ) const noexcept;

        /**
         * @brief Set the variable of name to a value.
         * 
         * Safer alternative to the `void *` version.
         */
        template <typename T>
        void SetVariable(
            StructuredBuffer & buf,
            const std::string & name,
            T val
        ) noexcept requires (std::is_standard_layout_v<T>) {
            assert(this->TypeCheck(name, typeid(T)));
            SetVariable(buf, name, &val);
        }

        /**
         * @brief Mark a variable as a structured buffer.
         * Use this to place another structured buffer into it.
         */
        void AddStructuredBuffer (
            const std::string & name,
            size_t offset,
            size_t buffer_size
        );

        /**
         * @brief Set the structured buffer variable.
         * 
         * The caller must ensure that the specified buffer is available when writing it.
         */
        void SetStructuredBuffer (
            StructuredBuffer & buf,
            const std::string & name,
            const StructuredBuffer & buffer
        );

        void WriteBuffer(
            const StructuredBuffer & data,
            std::byte * buffer
        ) const noexcept;

        void WriteBuffer(
            const StructuredBuffer & data,
            std::vector <std::byte> & buffer
        ) const;
    };
}

#endif // RENDER_MEMORY_STRUCTUREDBUFFERPLACER_INCLUDED
