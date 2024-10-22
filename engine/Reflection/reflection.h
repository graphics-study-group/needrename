#ifndef REFLECTION_REFLECTION_INCLUDED
#define REFLECTION_REFLECTION_INCLUDED

#include <vector>
#include <memory>

#include "Type.h"
#include "Field.h"
#include "Method.h"
#include "utils.h"

// Suppress warning from attributes
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

#define REFLECTION [[clang::annotate("reflection")]]

namespace Engine
{
    namespace Reflection
    {
        /// @brief Initialize the reflection system. Must be called before using any reflection features.
        void Initialize();

        /// @brief Register basic types such as int, float, double, etc. Called by Initialize.
        void RegisterBasicTypes();

        /// @brief Get the Reflection::Type class of an object. Note that this function is implemented in generated code.
        /// @tparam T the type of the object
        /// @param obj the object to get the type of
        /// @return the shared pointer to the Type class of the object
        template<typename T>
        std::shared_ptr<Type> GetTypeFromObject(const T &obj);

        /// @brief Get the Reflection::Type from a name
        /// @param name the type name
        /// @return the shared pointer to the Type class
        std::shared_ptr<Type> GetType(const std::string &name);
    }
}

#endif // REFLECTION_REFLECTION_INCLUDED
