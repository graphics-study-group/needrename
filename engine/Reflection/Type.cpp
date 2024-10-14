#include "Type.h"

namespace Engine
{
    namespace Reflection
    {
        std::unordered_map<std::string, std::shared_ptr<Type>> Type::s_typeMap;

        const std::string & Type::GetName() const
        {
            return m_name;
        }

        void Type::setName(const std::string & name)
        {
            m_name = name;
        }

        void Type::AddMethod(const std::string & name, std::function<void*(void*, void*, std::vector<void*>)> method)
        {
            if(m_methods.find(name) != m_methods.end())
                throw std::runtime_error("Method " + name + " already exists");
            m_methods[name] = method;
        }

        std::shared_ptr<void> Type::CreateInstance() const
        {
            return nullptr;
        }

        template<typename T>
        std::shared_ptr<T> Type::CreateInstance() const
        {
            return std::make_shared<T>();
        }
    }
}
