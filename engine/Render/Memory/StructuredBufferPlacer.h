#ifndef RENDER_MEMORY_STRUCTUREDBUFFERPLACER_INCLUDED
#define RENDER_MEMORY_STRUCTUREDBUFFERPLACER_INCLUDED

#include <memory>
#include <vector>
#include <type_traits>
#include <typeinfo>

namespace Engine {

    class StructuredBuffer;

    /**
     * @brief A helper that places named variables into a structured buffer.
     */
    class StructuredBufferPlacer {
        struct impl;
        std::unique_ptr <impl> pimpl;

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
         * @brief Mark a variable as a structured buffer.
         * Use this to place another structured buffer into it.
         * 
         * @param buffer The subbuffer. It is up to the caller to ensure:
         *          - Subbuffer outlives the main buffer;
         *          - No cycle exists in the chain of subbuffers.
         */
        void AddStructuredBuffer (
            const std::string & name,
            size_t offset,
            const StructuredBufferPlacer & buffer
        );

        /**
         * @brief Recursively determine the maximal size of this buffer.
         */
        size_t CalculateMaxSize() const noexcept;

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
