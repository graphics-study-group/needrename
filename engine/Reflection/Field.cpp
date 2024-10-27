#include "Field.h"
#include <stdexcept>
#include "Var.h"

namespace Engine
{
    namespace Reflection
    {
        Field::Field(const std::string &name, std::weak_ptr<Type> classtype, std::shared_ptr<Type> fieldtype, const WrapperFieldFunc &getter_func)
            :  m_getter(getter_func), m_name(name), m_classtype(classtype), m_fieldtype(fieldtype)
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
    }
}
