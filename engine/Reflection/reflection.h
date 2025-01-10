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
            static void RegisterNewType(const std::string &name, const std::type_info *type_info, bool reflectable = false);
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

        /// @brief Get the Reflection::Type from a name
        /// @param name the type name
        /// @param type_info the type_info of the type. default is nullptr, than the Type will be registered with nullptr type_info
        /// @return the shared pointer to the Type class
        std::shared_ptr<Type> GetType(const std::string &name, const std::type_info *type_info = nullptr);

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
            return GetType(typeid(obj).name(), &typeid(obj));
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
