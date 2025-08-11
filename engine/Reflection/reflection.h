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
        std::shared_ptr<const Type> GetType();

        /// @brief Get the Reflection::Type class of an object. If the object is polymorphic, it will return the most derived type. If the type is not registered, it will create a new Type without registering it.
        /// @tparam T the type of the object
        /// @param obj the object to get the type of
        /// @return the shared pointer to the Type class of the object
        template <typename T>
        std::shared_ptr<const Type> GetTypeFromObject(T &&obj);

        /// @brief Get the Reflection::Type from a name. Return nullptr if the type is not found.
        /// @param name the type name
        /// @return the shared pointer to the Type class. nullptr if the type is not found
        std::shared_ptr<const Type> GetType(const char *name);

        /// @brief Get the Reflection::Type from a name. Return nullptr if the type is not found.
        /// @param name the type name
        /// @return the shared pointer to the Type class. nullptr if the type is not found
        std::shared_ptr<const Type> GetType(const std::string &name);

        /// @brief Get the Reflection::Var class from an object.
        /// @param obj the object to get the Var of
        /// @return Var
        template <typename T>
        Var GetVar(T &&obj);

        template <typename T>
        std::shared_ptr<const Type> CreateType();
    }
}

namespace Engine
{
    namespace Reflection
    {
        template <typename T>
        std::shared_ptr<const Type> GetType()
        {
            std::type_index type_index = std::type_index(typeid(T));
            if (Type::s_index_type_map.find(type_index) == Type::s_index_type_map.end())
            {
                return CreateType<T>();
            }
            return Type::s_index_type_map[type_index];
        }

        template <typename T>
        std::shared_ptr<const Type> GetTypeFromObject(T &&obj)
        {
            if (typeid(T) == typeid(obj))
                return GetType<T>();
            std::type_index type_index = std::type_index(typeid(obj));
            if (Type::s_index_type_map.find(type_index) == Type::s_index_type_map.end())
            {
                // Don't need support special types like std::vector, std::string, etc.
                // because the object is polymorphic, and all the special types are not derived from any base type.
                return std::shared_ptr<const Type>(new Type(type_index.name(), sizeof(obj), false));
            }
            return Type::s_index_type_map[type_index];
        }

        template <typename T>
        Var GetVar(T &&obj)
        {
            return Var(GetTypeFromObject(obj), (void *)&obj);
        }

        template <typename T>
        concept is_std_vector = requires {
            typename T::value_type;
            requires std::is_same_v<T, std::vector<typename T::value_type>>;
        };

        template <typename T>
        std::shared_ptr<const Type> CreateType()
        {
            if constexpr (std::is_pointer_v<T>)
            {
                return std::shared_ptr<const PointerType>(new PointerType(GetType<std::remove_pointer_t<T>>(), sizeof(T), PointerType::PointerTypeKind::Raw));
            }
            else if constexpr (std::is_array_v<T>)
            {
                return std::shared_ptr<const ArrayType>(new ArrayType(GetType<std::remove_extent_t<T>>(), sizeof(T), ArrayType::ArrayTypeKind::Raw));
            }
            else if constexpr (is_std_vector<T>)
            {
                return std::shared_ptr<const ArrayType>(new ArrayType(GetType<typename T::value_type>(), sizeof(T), ArrayType::ArrayTypeKind::StdVector));
            }
            else if constexpr (std::is_void_v<T>)
            {
                throw std::runtime_error("The void type should be created in initialization");
            }
            return std::shared_ptr<const Type>(new Type(typeid(std::remove_const_t<T>).name(), sizeof(T), false));
        }
    }
}

#endif // REFLECTION_REFLECTION_INCLUDED
