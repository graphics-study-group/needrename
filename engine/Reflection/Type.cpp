#include "Type.h"
#include <cstdarg>

namespace Engine
{
    namespace Reflection
    {
        // class Type
        std::unordered_map<std::string, std::shared_ptr<Type>> Type::s_type_map;

        Type::Type(const std::string &name)
            : m_name(name)
        {
            // TODO: better way to store type of basic types
            if (name == "void" || name == typeid(void).name())
                m_type_info = &typeid(void);
            else if (name == "int" || name == typeid(int).name())
                m_type_info = &typeid(int);
            else if (name == "float" || name == typeid(float).name())
                m_type_info = &typeid(float);
            else if (name == "double" || name == typeid(double).name())
                m_type_info = &typeid(double);
            else if (name == "char" || name == typeid(char).name())
                m_type_info = &typeid(char);
            else if (name == "bool" || name == typeid(bool).name())
                m_type_info = &typeid(bool);
            else if (name == "short" || name == typeid(short).name())
                m_type_info = &typeid(short);
            else if (name == "long" || name == typeid(long).name())
                m_type_info = &typeid(long);
            else if (name == "unsigned int" || name == typeid(unsigned int).name())
                m_type_info = &typeid(unsigned int);
            else if (name == "unsigned char" || name == typeid(unsigned char).name())
                m_type_info = &typeid(unsigned char);
            else if (name == "unsigned short" || name == typeid(unsigned short).name())
                m_type_info = &typeid(unsigned short);
            else if (name == "unsigned long" || name == typeid(unsigned long).name())
                m_type_info = &typeid(unsigned long);
            else if (name == "long long" || name == typeid(long long).name())
                m_type_info = &typeid(long long);
            else if (name == "unsigned long long" || name == typeid(unsigned long long).name())
                m_type_info = &typeid(unsigned long long);
            else
                m_type_info = nullptr;
            this->reflectable = false;
        }

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

        void Type::AddMethod(const std::string &name, std::shared_ptr<Method> method)
        {
            if (m_methods.find(name) != m_methods.end())
                throw std::runtime_error("Method " + name + " already exists");
            m_methods[name] = method;
        }

        void Type::AddBaseType(std::shared_ptr<Type> base_type)
        {
            m_base_type.push_back(base_type);
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
