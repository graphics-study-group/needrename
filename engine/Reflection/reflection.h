#ifndef REFLECTION_REFLECTION_INCLUDED
#define REFLECTION_REFLECTION_INCLUDED

#include <vector>
#include <memory>
#include <cassert>

#include "Type.h"
#include "Field.h"
#include "Method.h"
#include "utils.h"
#include "macros.h"

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

        /// @brief Get the Reflection::Type class of a template type T. If the type is not registered, it will create a new Type without registering it.
        /// @tparam T the type to get the Type of
        /// @return the shared pointer to the Type class of the type
        template <typename T>
        std::shared_ptr<Type> GetType();

        /// @brief Get the Reflection::Type class of an object. If the object is polymorphic, it will return the most derived type. If the type is not registered, it will create a new Type without registering it.
        /// @tparam T the type of the object
        /// @param obj the object to get the type of
        /// @return the shared pointer to the Type class of the object
        template <typename T>
        std::shared_ptr<Type> GetTypeFromObject(const T &obj);

        /// @brief Get the Reflection::Type from a name. Return nullptr if the type is not found.
        /// @param name the type name
        /// @return the shared pointer to the Type class. nullptr if the type is not found
        std::shared_ptr<Type> GetType(const char *name);

        /// @brief Get the Reflection::Type from a name. Return nullptr if the type is not found.
        /// @param name the type name
        /// @return the shared pointer to the Type class. nullptr if the type is not found
        std::shared_ptr<Type> GetType(const std::string &name);

        /// @brief Get the Reflection::Var class from an object.
        /// @param obj the object to get the Var of
        /// @return Var
        template <typename T>
        Var GetVar(T &obj);

        /// @brief Get the Reflection::ConstVar class from an object.
        /// @param obj the object to get the ConstVar of
        /// @return ConstVar
        template <typename T>
        ConstVar GetConstVar(const T &obj);

        template <typename T>
        std::shared_ptr<Type> CreateType(const std::string &name = "");
    }
}

namespace Engine
{
    namespace Reflection
    {
        template <typename T>
        std::shared_ptr<Type> GetType()
        {
            std::type_index type_index = std::type_index(typeid(T));
            if (Type::s_index_type_map.find(type_index) == Type::s_index_type_map.end())
            {
                return CreateType<T>();
            }
            return Type::s_index_type_map[type_index];
        }

        template <typename T>
        std::shared_ptr<Type> GetTypeFromObject(const T &obj)
        {
            if (typeid(T) == typeid(obj))
                return GetType<T>();
            std::type_index type_index = std::type_index(typeid(obj));
            if (Type::s_index_type_map.find(type_index) == Type::s_index_type_map.end())
            {
                // Don't need support special types like std::vector, std::string, etc.
                // because the object is polymorphic, and all the special types are not derived from any base type.
                return std::shared_ptr<Type>(new Type(type_index.name(), false));
            }
            return Type::s_index_type_map[type_index];
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

        template <typename T>
        std::shared_ptr<Type> CreateType(const std::string &name)
        {
            // TODO: support special types like std::vector, std::string, etc.
            if (name.empty())
            {
                return std::shared_ptr<Type>(new Type(typeid(T).name(), false));
            }
            return std::shared_ptr<Type>(new Type(name, false));
        }
    }
}

#endif // REFLECTION_REFLECTION_INCLUDED
