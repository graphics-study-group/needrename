#include "Type.h"
#include <cstdarg>

namespace Engine
{
    namespace Reflection
    {
        std::unordered_map<std::string, std::shared_ptr<Type>> Type::s_type_map;

        WrapperMemberFunc Type::FindFunction(const std::string &name)
        {
            if (m_methods.find(name) != m_methods.end())
                return m_methods[name];
            for (auto &base_type : m_base_type)
            {
                auto func = base_type->FindFunction(name);
                if (func)
                    return func;
            }
            return nullptr;
        }

        const std::string &Type::GetName() const
        {
            return m_name;
        }

        void Type::setName(const std::string &name)
        {
            m_name = name;
        }

        void Type::AddMethod(const std::string &name, WrapperMemberFunc method)
        {
            if (m_methods.find(name) != m_methods.end())
                throw std::runtime_error("Method " + name + " already exists");
            m_methods[name] = method;
        }

        void Type::AddBaseType(std::shared_ptr<Type> base_type)
        {
            m_base_type.push_back(base_type);
        }
    }
}
