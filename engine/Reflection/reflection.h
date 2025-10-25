#ifndef REFLECTION_REFLECTION_INCLUDED
#define REFLECTION_REFLECTION_INCLUDED

#include <cassert>
#include <charconv>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "Field.h"
#include "Method.h"
#include "Type.h"
#include "macros.h"
#include "utils.h"

namespace Engine {
    namespace Reflection {
        class Registrar {
        public:
            /// @brief Register a new type.
            static void RegisterNewType(const std::string &name, std::type_index type_index, bool reflectable = false);
            /// @brief Register basic types such as int, float, double, etc. Called by Initialize.
            static void RegisterBasicTypes();
        };

        /// @brief Initialize the reflection system. Must be called before using any reflection features.
        void Initialize();

        /// @brief Get the Reflection::Type class of a template type T. If the type is not registered, it will create a
        /// new Type without registering it.
        /// @tparam T the type to get the Type of
        /// @return the shared pointer to the Type class of the type
        template <typename T>
        std::shared_ptr<const Type> GetType();

        /// @brief Get the Reflection::Type class of an object. If the object is polymorphic, it will return the most
        /// derived type. If the type is not registered, it will create a new Type without registering it.
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
    } // namespace Reflection
} // namespace Engine

namespace Engine {
    namespace Reflection {
        template <typename T>
        std::shared_ptr<const Type> GetType() {
            std::type_index type_index = std::type_index(typeid(T));
            if (Type::s_index_type_map.find(type_index) == Type::s_index_type_map.end()) {
                return CreateType<T>();
            }
            if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
                return std::shared_ptr<const Type>(new ConstType(Type::s_index_type_map[type_index]));
            }
            return Type::s_index_type_map[type_index];
        }

        template <typename T>
        std::shared_ptr<const Type> GetTypeFromObject(T &&obj) {
            if (typeid(T) == typeid(obj)) return GetType<T>();
            std::type_index type_index = std::type_index(typeid(obj));
            std::shared_ptr<const Type> ret;
            if (Type::s_index_type_map.find(type_index) == Type::s_index_type_map.end()) {
                // Don't need support special types like std::vector, std::string, etc.
                // because the object is polymorphic, and all the special types are not derived from any base type.
                ret = std::shared_ptr<const Type>(new Type(type_index.name(), sizeof(obj), false));
            } else {
                ret = Type::s_index_type_map[type_index];
            }
            if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
                return std::shared_ptr<const Type>(new ConstType(ret));
            }
            return ret;
        }

        template <typename T>
        Var GetVar(T &&obj) {
            return Var(GetTypeFromObject(obj), (void *)&obj);
        }

        template <typename T>
        concept is_std_vector = requires {
            typename T::value_type;
            requires std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>>;
        };

        template <typename T>
        concept is_std_shared_ptr = requires {
            typename T::element_type;
            requires std::is_same_v<T, std::shared_ptr<typename T::element_type>>;
        };

        template <typename T>
        concept is_std_weak_ptr = requires {
            typename T::element_type;
            requires std::is_same_v<T, std::weak_ptr<typename T::element_type>>;
        };

        template <typename T>
        concept is_std_unique_ptr = requires {
            typename T::element_type;
            requires std::is_same_v<T, std::unique_ptr<typename T::element_type>>;
        };

        template <typename T>
        std::shared_ptr<const Type> CreateType() {
            auto deleter = [](void *obj) {
                delete static_cast<std::add_pointer_t<T>>(obj);
            };
            if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
                return std::shared_ptr<const Type>(new ConstType(GetType<std::remove_cvref_t<T>>()));
            } else if constexpr (std::is_pointer_v<T>) {
                return std::shared_ptr<const PointerType>(
                    new PointerType(GetType<std::remove_pointer_t<T>>(), sizeof(T), PointerType::PointerTypeKind::Raw, deleter)
                );
            } else if constexpr (is_std_shared_ptr<T>) {
                return std::shared_ptr<const PointerType>(new PointerType(
                    GetType<typename T::element_type>(), sizeof(T), PointerType::PointerTypeKind::Shared, deleter
                ));
            } else if constexpr (is_std_weak_ptr<T>) {
                return std::shared_ptr<const PointerType>(
                    new PointerType(GetType<typename T::element_type>(), sizeof(T), PointerType::PointerTypeKind::Weak, deleter)
                );
            } else if constexpr (is_std_unique_ptr<T>) {
                return std::shared_ptr<const PointerType>(new PointerType(
                    GetType<typename T::element_type>(), sizeof(T), PointerType::PointerTypeKind::Unique, deleter
                ));
            }
            if constexpr (std::is_void_v<T>) {
                throw std::runtime_error("The void type should be created in initialization");
            } else {
                return std::shared_ptr<const Type>(new Type(typeid(std::remove_const_t<T>).name(), sizeof(T), false, deleter));
            }
        }
    } // namespace Reflection
} // namespace Engine

#endif // REFLECTION_REFLECTION_INCLUDED
