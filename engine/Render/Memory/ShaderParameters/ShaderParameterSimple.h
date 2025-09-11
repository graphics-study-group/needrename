#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERSCALAR_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERSCALAR_INCLUDED

#include <variant>
#include <cstdint>
#include <vector>
#include <string>

namespace Engine {
    namespace ShdrRfl {
        struct SPVariable {};
        struct SPAssignable : SPVariable {};

        /**
         * @brief Shader variables that can be directly assigned
         * by C++ types, e.g. `float`, `vec4` and `mat4`.
         * 
         * These variables won't need type information.
         */
        struct SPSimpleAssignable : SPAssignable {
            uint32_t absolute_offset;
            SPVariable * parent_interface;
        };
        struct SPScalar : SPSimpleAssignable {
            enum class Type {
                Uint,
                Sint,
                Float
            } type;
        };

        /* struct SPArray : SPTransparentAssignable {
            std::variant<SPArray, SPSimple> underlying_type;
            uint32_t stride;
            uint32_t offset;
        }; */

        struct SPVec4 : SPSimpleAssignable {

        };
        struct SPMat4 : SPSimpleAssignable {

        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETERSCALAR_INCLUDED
