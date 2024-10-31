#include "Field.h"
#include <stdexcept>
#include "Var.h"

namespace Engine
{
    namespace Reflection
    {
        Field::Field(const std::string &name, std::weak_ptr<Type> classtype, std::shared_ptr<Type> fieldtype, const WrapperFieldFunc &getter_func, const WrapperConstFieldFunc &const_getter_func)
            :  m_getter(getter_func), m_const_getter(const_getter_func), m_name(name), m_classtype(classtype), m_fieldtype(fieldtype)
        {
        }

        Var Field::GetVar(Var &obj)
        {
            if (obj.m_type != m_classtype.lock())
                throw std::runtime_error("Invalid object type");
            void *data = nullptr;
            m_getter(obj.GetDataPtr(), data);
            return Var(m_fieldtype, data);
        }

        Var Field::GetVar(void *obj)
        {
            void *data = nullptr;
            m_getter(obj, data);
            return Var(m_fieldtype, data);
        }

        ConstVar Field::GetConstVar(ConstVar &obj)
        {
            if (obj.m_type != m_classtype.lock())
                throw std::runtime_error("Invalid object type");
            const void *data = nullptr;
            m_const_getter(obj.GetDataPtr(), data);
            return ConstVar(m_fieldtype, data);
        }

        ConstVar Field::GetConstVar(const void *obj)
        {
            const void *data = nullptr;
            m_const_getter(obj, data);
            return ConstVar(m_fieldtype, data);
        }
    }
}
