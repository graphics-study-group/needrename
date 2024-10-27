#include "Type.h"
#include <cstdarg>

namespace Engine
{
    namespace Reflection
    {
        // class Type
        std::unordered_map<std::string, std::shared_ptr<Type>> Type::s_type_map;

        Type::Type(const std::string &name, const std::type_info *type_info, bool reflectable)
            : m_name(name), m_type_info(type_info), reflectable(reflectable)
        {
        }

        std::shared_ptr<Method> Type::GetMethodFromManagedName(const std::string &name)
        {
            if (m_methods.find(name) != m_methods.end())
                return m_methods[name];
            for (auto &base_type : m_base_type)
            {
                auto func = base_type->GetMethodFromManagedName(name);
                if (func)
                    return func;
            }
            return nullptr;
        }

        const std::string &Type::GetName() const
        {
            return m_name;
        }

        void Type::SetName(const std::string &name)
        {
            m_name = name;
        }

        void Type::AddMethod(std::shared_ptr<Method> method)
        {
            if (m_methods.find(method->m_name) != m_methods.end())
                throw std::runtime_error("Method " + method->m_name + " already exists");
            m_methods[method->m_name] = method;
        }

        void Type::AddBaseType(std::shared_ptr<Type> base_type)
        {
            m_base_type.push_back(base_type);
        }

        void Type::AddField(const std::shared_ptr<Type> field_type, const std::string &name, const WrapperFieldFunc &field)
        {
            m_fields[name] = std::shared_ptr<Field>(new Field(name, shared_from_this(), field_type, field));
        }

        void Type::AddField(const std::shared_ptr<Field> field)
        {
            if (m_fields.find(field->m_name) != m_fields.end())
                throw std::runtime_error("Field " + field->m_name + " already exists");
            m_fields[field->m_name] = field;
        }

        std::shared_ptr<Field> Type::GetField(const std::string &name)
        {
            if (m_fields.find(name) != m_fields.end())
                return m_fields[name];
            for (auto &base_type : m_base_type)
            {
                auto field = base_type->GetField(name);
                if (field)
                    return field;
            }
            return nullptr;
        }
    }
}
