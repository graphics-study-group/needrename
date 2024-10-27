#ifndef REFLECTION_TYPE_INCLUDED
#define REFLECTION_TYPE_INCLUDED

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include "utils.h"

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine
{
    namespace Reflection
    {
        class TypeRegistrar;
        class Registrar;
        class Field;
        class Method;
        class Var;

        /// @brief The Type class represents a type in the reflection system.
        /// It contains information about the type such as its name, base types, fields, and methods.
        /// Note that only reflected types and some basic types use their own name. The other types use their type_info name.
        class Type : public std::enable_shared_from_this<Type>
        {
        public:
            static constexpr const char *k_constructor_name = "$Constructor";
            static std::unordered_map<std::string, std::shared_ptr<Type>> s_type_map;

        protected:
            friend class Registrar;
            friend class TypeRegistrar;
            Type() = delete;
            Type(const std::string &name, const std::type_info *type_info, bool reflectable = false);

            // suppress the warning of -Weffc++
            Type(const Type &) = delete;
            void operator=(const Type &) = delete;
        public:
            virtual ~Type() = default;
        
        private:
            std::shared_ptr<Method> GetMethodFromManagedName(const std::string &name);

        protected:
            std::vector<std::shared_ptr<Type>> m_base_type{};
            std::unordered_map<std::string, std::shared_ptr<Field>> m_fields{};
            std::unordered_map<std::string, std::shared_ptr<Method>> m_methods{};

            void SetName(const std::string &name);
            template <typename... Args>
            void AddConstructor(const WrapperMemberFunc &func);
            template <typename T>
            void AddMethod(const std::string &name, const WrapperMemberFunc &func, std::shared_ptr<Type> return_type, T original_func);
            void AddMethod(std::shared_ptr<Method> method);
            void AddBaseType(std::shared_ptr<Type> base_type);
            void AddField(const std::shared_ptr<Type> field_type, const std::string &name, const WrapperFieldFunc &field_getter);
            void AddField(const std::shared_ptr<Field> field);

        public:
            std::string m_name{};
            const std::type_info *m_type_info{};
            bool reflectable = false;

            const std::string &GetName() const;
            template <typename... Args>
            Var CreateInstance(Args... args);
            template <typename... Args>
            std::shared_ptr<Method> GetMethod(const std::string &name, Args... args);
            std::shared_ptr<Field> GetField(const std::string &name);
        };
    }
}

#pragma GCC diagnostic pop

#include "Type.tpp"

#endif // REFLECTION_TYPE_INCLUDED
