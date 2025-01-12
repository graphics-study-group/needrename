#ifndef REFLECTION_REFLECTION_INCLUDED
#define REFLECTION_REFLECTION_INCLUDED

#include <vector>
#include <memory>

#include "Type.h"
#include "Field.h"
#include "Method.h"
#include "utils.h"
#include "macros.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

namespace Engine
{
    namespace Reflection
    {
        class Registrar
        {
        public:
            /// @brief Register a new type.
            static void RegisterNewType(const std::string &name, std::type_index type_index, bool reflectable = false);
            /// @brief Register basic types such as int, float, double, etc. Called by Initialize.
            static void RegisterBasicTypes();
        };

        /// @brief Initialize the reflection system. Must be called before using any reflection features.
        void Initialize();

        /// @brief Get the Reflection::Type class of an object. Note that this function is implemented in generated code.
        /// @tparam T the type of the object
        /// @param obj the object to get the type of
        /// @return the shared pointer to the Type class of the object
        template<typename T>
        std::shared_ptr<Type> GetTypeFromObject(const T &obj);

        /// @brief Get the Reflection::Type from a name. Return nullptr if the type is not found.
        /// @param name the type name
        /// @return the shared pointer to the Type class. nullptr if the type is not found
        std::shared_ptr<Type> GetType(const std::string &name);

        /// @brief Get the Reflection::Type from a type_index. Return nullptr if the type is not found.
        /// @param type_index the type_index of the type
        /// @return the shared pointer to the Type class. nullptr if the type is not found
        std::shared_ptr<Type> GetType(const std::type_index &type_index);

        /// @brief Get the Reflection::Type from a type_index. If the type is not found, create a new type with the type_index.
        /// @param type_index the type_index of the type
        /// @param name the name of the type for registeration. Note that the type_index.name() will always be registered. If the @param name is not empty, it will be registered as well.
        /// @return the shared pointer to the Type class
        std::shared_ptr<Type> GetOrCreateType(std::type_index type_index, const std::string &name = "");

        /// @brief Get the Reflection::Var class from an object.
        /// @param obj the object to get the Var of
        /// @return Var
        template<typename T>
        Var GetVar(T &obj);

        /// @brief Get the Reflection::ConstVar class from an object.
        /// @param obj the object to get the ConstVar of
        /// @return ConstVar
        template<typename T>
        ConstVar GetConstVar(const T &obj);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace Engine
{
    namespace Reflection
    {
        template <typename T>
        std::shared_ptr<Type> GetTypeFromObject(const T &obj)
        {
            return GetOrCreateType(std::type_index(typeid(obj)));
        }

        template <typename T>
        Var GetVar(const T &obj)
        {
            return Var(GetTypeFromObject(obj), &obj);
        }

        template <typename T>
        ConstVar GetConstVar(const T &obj)
        {
            return ConstVar(GetTypeFromObject(obj), &obj);
        }
    }
}

#pragma GCC diagnostic pop

#endif // REFLECTION_REFLECTION_INCLUDED
