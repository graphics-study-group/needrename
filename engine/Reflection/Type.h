#ifndef REFLECTION_TYPE_INCLUDED
#define REFLECTION_TYPE_INCLUDED

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Engine
{
    namespace Reflection
    {
        class Registrar;
        class Field;
        class Method;
        class Var;

        class Type : public std::enable_shared_from_this<Type>
        {
        public:
            static constexpr const char *constructer_name = "$Constructor";
            static std::unordered_map<std::string, std::shared_ptr<Type>> s_type_map;

        public:
            Type() = delete;
            Type(const std::string &name);
            Type(const std::string &name, const std::type_info *type_info, bool reflectable = false);
            virtual ~Type() = default;
        
        private:
            std::shared_ptr<Method> GetMethodFromManagedName(const std::string &name);

        protected:
            friend class Registrar;

            std::vector<std::shared_ptr<Type>> m_base_type{};
            std::unordered_map<std::string, std::shared_ptr<Field>> m_fields{};
            std::unordered_map<std::string, std::shared_ptr<Method>> m_methods{};

            void SetName(const std::string &name);
            void AddMethod(const std::string &name, std::shared_ptr<Method> method);
            void AddBaseType(std::shared_ptr<Type> base_type);
            template <typename T>
            void AddField(const std::shared_ptr<Type> field_type, const std::string &name, T field);

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

#include "Type.tpp"

#endif // REFLECTION_TYPE_INCLUDED
