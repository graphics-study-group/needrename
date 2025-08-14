#ifndef REFLECTION_TYPE_INCLUDED
#define REFLECTION_TYPE_INCLUDED

#include "utils.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine {
    namespace Reflection {
        class Registrar;
        class Field;
        class ArrayField;
        class Method;
        class Var;

        /// @brief The Type class represents a type in the reflection system.
        /// It contains information about the type such as its name, base types, fields, and methods.
        /// Note that only reflected types and some basic types use their own name. The other types use their type_info
        /// name.
        class Type : public std::enable_shared_from_this<Type> {
        public:
            // The name of the constructor method
            static constexpr const char *k_constructor_name = "$Constructor";
            // The map between std::type_index and type
            static std::unordered_map<std::type_index, std::shared_ptr<const Type>> s_index_type_map;
            // The map between type name and std::type_index
            static std::unordered_map<std::string, std::type_index> s_name_index_map;

            enum class TypeKind {
                None,
                Const,
                Pointer
            };

        public:
            Type() = delete;
            /// @brief Construct a new Type object.
            /// @param name Type name
            /// @param size Type size
            /// @param reflectable whether the type is reflectable
            Type(const std::string &name, size_t size, bool reflectable = false);

            // suppress the warning of -Weffc++
            Type(const Type &) = delete;
            void operator=(const Type &) = delete;

        public:
            virtual ~Type() = default;

        private:
            std::shared_ptr<const Method> GetMethodFromMangledName(const std::string &name) const;

        protected:
            std::vector<std::shared_ptr<const Type>> m_base_type{};
            std::unordered_map<std::string, std::shared_ptr<const Field>> m_fields{};
            std::unordered_map<std::string, std::shared_ptr<const ArrayField>> m_array_fields{};
            std::unordered_map<std::string, std::shared_ptr<const Method>> m_methods{};

            std::string m_name{};
            // The size of the type in bytes
            size_t m_size = 0;
            // Whether the type is reflectable
            bool m_reflectable = false;
            TypeKind m_kind = TypeKind::None;

        public:
            // Add a constructor to the type
            template <typename... Args>
            void AddConstructor(const WrapperMemberFunc &func);

            /// @brief Add a member function to the type.
            /// @param name the name of the function
            /// @param func the member function wrapper
            /// @param return_type the return type of the function
            /// @param is_const whether the function is const
            template <typename... Args>
            void AddMethod(
                const std::string &name,
                const WrapperMemberFunc &func,
                std::shared_ptr<const Type> return_type,
                bool is_const,
                bool return_value_needs_free
            );

            /// @brief Add a member function to the type.
            void AddMethod(std::shared_ptr<const Method> method);
            /// @brief Add a base type to the type.
            /// @param base_type
            void AddBaseType(std::shared_ptr<const Type> base_type);

            /// @brief Add a field to the type.
            /// @param field_type the type of the field
            /// @param name
            /// @param field_getter the wrapper function for getting the field
            /// @param const_field_getter the wrapper function for getting the field when the object is const
            void AddField(
                const std::shared_ptr<const Type> field_type,
                const std::string &name,
                const WrapperFieldFunc &field_getter
            );
            /// @brief Add a field to the type.
            void AddField(const std::shared_ptr<const Field> field);

            /// @brief Add an array field to the type.
            /// @param element_type the type of the elements in the array
            /// @param name the name of the array field
            /// @param array_getter_func the wrapper function for getting the array field
            void AddArrayField(
                const std::shared_ptr<const Type> element_type,
                const std::string &name,
                const WrapperArrayFieldFunc &array_getter_func,
                const WrapperArrayFieldSize &array_size_getter_func,
                const WrapperArrayResizeFunc &array_resize_func
            );

            // Get the name of the type
            const std::string &GetName() const;
            size_t GetTypeSize() const;
            bool IsReflectable() const;
            TypeKind GetTypeKind() const;

            /// @brief Create an instance of the type.
            /// @param ...args arguments for the constructor
            /// @return a Var object representing the instance
            template <typename... Args>
            Var CreateInstance(Args &&...args) const;

            /// @brief Get a method of the type.
            /// @param name the name of the method
            /// @param ...args the arguments to the method. We need arguments to deal with overloaded methods.
            /// @return the shared pointer to the Method object
            template <typename... Args>
            std::shared_ptr<const Method> GetMethod(const std::string &name, Args &&...args) const;

            /// @brief Get a field of the type.
            /// @param name the name of the field
            /// @return the shared pointer to the Field object
            std::shared_ptr<const Field> GetField(const std::string &name) const;

            /// @brief Get an array field of the type.
            /// @param name the name of the array field
            /// @return the shared pointer to the ArrayField object
            std::shared_ptr<const ArrayField> GetArrayField(const std::string &name) const;

            /// @brief Get the unordered map of fields of the type.
            const std::unordered_map<std::string, std::shared_ptr<const Field>> &GetFields() const;

            /// @brief Get the unordered map of array fields of the type.
            const std::unordered_map<std::string, std::shared_ptr<const ArrayField>> &GetArrayFields() const;
        };

        class ConstType : public Type {
        public:
            ConstType(std::shared_ptr<const Type> base_type);
            virtual ~ConstType() = default;

            std::shared_ptr<const Type> m_base_type;
        };

        class PointerType : public Type {
        public:
            static std::unordered_map<std::type_index, WrapperSmartPointerGet> s_shared_pointer_getter_map;
            static std::unordered_map<std::type_index, WrapperSmartPointerGet> s_weak_pointer_getter_map;
            static std::unordered_map<std::type_index, WrapperSmartPointerGet> s_unique_pointer_getter_map;

            template <typename T>
            static void RegisterSmartPointerGetFunc() {
                s_shared_pointer_getter_map[std::type_index(typeid(T))] = [](void *ptr) -> void * {
                    return static_cast<void *>(static_cast<std::shared_ptr<T> *>(ptr)->get());
                };
                s_weak_pointer_getter_map[std::type_index(typeid(T))] = [](void *ptr) -> void * {
                    return static_cast<void *>(static_cast<std::weak_ptr<T> *>(ptr)->lock().get());
                };
                s_unique_pointer_getter_map[std::type_index(typeid(T))] = [](void *ptr) -> void * {
                    return static_cast<void *>(static_cast<std::unique_ptr<T> *>(ptr)->get());
                };
            }

        public:
            enum class PointerTypeKind {
                Raw,
                Shared,
                Weak,
                Unique
            };
            PointerType(std::shared_ptr<const Type> pointed_type, size_t size, PointerTypeKind kind);
            virtual ~PointerType() = default;

        protected:
            std::shared_ptr<const Type> m_pointed_type;
            PointerTypeKind m_pointer_kind;

        public:
            std::shared_ptr<const Type> GetPointedType() const;
            PointerTypeKind GetPointerTypeKind() const;
        };
    } // namespace Reflection
} // namespace Engine

#pragma GCC diagnostic pop

#include "Type.tpp"

#endif // REFLECTION_TYPE_INCLUDED
