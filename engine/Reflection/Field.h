#ifndef REFLECTION_FIELD_INCLUDED
#define REFLECTION_FIELD_INCLUDED

#include <memory>
#include <cstdint>
#include "utils.h"

namespace Engine
{
    namespace Reflection
    {
        class Type;
        class Var;
        class ConstVar;

        class Field
        {
        protected:
            friend class Type;
            Field() = delete;
            Field(const std::string &name, std::weak_ptr<Type> classtype, std::shared_ptr<Type> fieldtype, const WrapperFieldFunc &getter_func, const WrapperConstFieldFunc &const_getter_func);
        public:
            ~Field() = default;
        
        protected:
            WrapperFieldFunc m_getter;
            WrapperConstFieldFunc m_const_getter;
        
        public:
            std::string m_name;
            std::weak_ptr<Type> m_classtype;
            std::shared_ptr<Type> m_fieldtype;

            Var GetVar(Var &obj);
            Var GetVar(void *obj);
            ConstVar GetConstVar(ConstVar &obj);
            ConstVar GetConstVar(const void *obj);
        };
    }
}

#endif // REFLECTION_FIELD_INCLUDED
