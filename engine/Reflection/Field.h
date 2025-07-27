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
            Field(const std::string &name, std::weak_ptr<Type> classtype, std::shared_ptr<const Type> fieldtype, const WrapperFieldFunc &getter_func, const WrapperConstFieldFunc &const_getter_func);
        public:
            ~Field() = default;
        
        protected:
            WrapperFieldFunc m_getter;
            WrapperConstFieldFunc m_const_getter;
        
        public:
            std::string m_name;
            std::weak_ptr<Type> m_classtype;
            std::shared_ptr<const Type> m_fieldtype;

            Var GetVar(Var &obj) const;
            Var GetVar(void *obj) const;
            ConstVar GetConstVar(ConstVar &obj) const;
            ConstVar GetConstVar(const void *obj) const;
        };
    }
}

#endif // REFLECTION_FIELD_INCLUDED
