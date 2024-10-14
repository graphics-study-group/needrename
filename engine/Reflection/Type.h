#ifndef REFLECTION_TYPE_INCLUDED
#define REFLECTION_TYPE_INCLUDED

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Engine
{
    namespace Reflection
    {
        class Type
        {
        public:
            static std::unordered_map<std::string, std::shared_ptr<Type>> s_typeMap;

        public:
            Type() = default;
            virtual ~Type() = default;
        
        protected:
            std::string m_name {};
            //std::unordered_map<std::string, std::shared_ptr<Field>> m_fields {};
            std::unordered_map<std::string, std::function<void*(void*, void*, std::vector<void*>)>> m_methods {};
        
        public:
            const std::string & GetName() const;
            void setName(const std::string & name);
            void AddMethod(const std::string & name, std::function<void*(void*, void*, std::vector<void*>)> method);

            std::shared_ptr<void> CreateInstance() const;
            template<typename T> std::shared_ptr<T> CreateInstance() const;
        };
    }
}

#endif // REFLECTION_TYPE_INCLUDED
