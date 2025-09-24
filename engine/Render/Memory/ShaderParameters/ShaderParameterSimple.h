#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERSCALAR_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERSCALAR_INCLUDED

#include <variant>
#include <cstdint>
#include <vector>
#include <string>

namespace Engine {
    namespace ShdrRfl {
        class SPType;
        class SPInterface;

        struct SPVariable {
            std::string name {};
            virtual ~SPVariable(){};
        };
        struct SPAssignable : SPVariable {};
        /**
         * @brief An assignable variable that is located in
         * an interface (i.e. UBO or SSBO)
         */
        struct SPAssignableInterface : SPAssignable {
            uint32_t absolute_offset {};
            SPInterface * parent_interface {nullptr};
        };

        /**
         * @brief Shader variables that can be directly assigned
         * by C++ types, e.g. `float`, `vec4` and `mat4`.
         * 
         * These variables won't need type information.
         */
        struct SPAssignableSimple : SPAssignableInterface {
            enum class Type {
                Unknown,
                Uint,
                Sint,
                Float,
                FVec4,
                FMat4
            } type {Type::Unknown};
        };

        struct SPAssignableArray : SPAssignableInterface {
            const SPType * underlying_type {nullptr};
        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETERSCALAR_INCLUDED
