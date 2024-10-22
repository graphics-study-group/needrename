#ifndef REFLECTION_FIELD_INCLUDED
#define REFLECTION_FIELD_INCLUDED

#include <memory>
#include <cstdint>

namespace Engine
{
    namespace Reflection
    {
        class Type;
        class Var;

        class Field
        {
        public:
            Field() = delete;
            Field(const std::string &name, std::weak_ptr<Type> classtype, std::shared_ptr<Type> fieldtype, std::uintptr_t offset);
            ~Field() = default;
        
        protected:
            std::uintptr_t m_offset;
        
        public:
            std::string m_name;
            std::weak_ptr<Type> m_classtype;
            std::shared_ptr<Type> m_fieldtype;

            Var GetVar(Var &obj);
            Var GetVar(void *obj);
        };
    }
}

#endif // REFLECTION_FIELD_INCLUDED
