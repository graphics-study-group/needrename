#include "Field.h"
#include <stdexcept>
#include "Var.h"

namespace Engine
{
    namespace Reflection
    {
        Field::Field(const std::string &name, std::weak_ptr<Type> classtype, std::shared_ptr<Type> fieldtype, std::uintptr_t offset)
            : m_name(name), m_classtype(classtype), m_fieldtype(fieldtype), m_offset(offset)
        {
        }

        Var Field::GetVar(Var &obj)
        {
            if (obj.m_type != m_classtype.lock())
                throw std::runtime_error("Invalid object type");
            return Var(m_fieldtype, reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(obj.GetDataPtr()) + m_offset));
        }

        Var Field::GetVar(void *obj)
        {
            return Var(m_fieldtype, reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(obj) + m_offset));
        }
    }
}
