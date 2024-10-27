#include "Var.h"
#include <stdexcept>
#include "Type.h"
#include "Field.h"

namespace Engine
{
    namespace Reflection
    {
        Var::Var(std::shared_ptr<Type> type, void *data)
            :  m_data(data), m_type(type)
        {
        }

        void *Var::GetDataPtr()
        {
            return m_data;
        }

        Var Var::GetMember(const std::string &name)
        {
            auto field = m_type->GetField(name);
            if (!field)
                throw std::runtime_error("Field not found");
            return field->GetVar(m_data);
        }

        Var &Var::operator=(const Var &var)
        {
            m_type = var.m_type;
            m_data = var.m_data;
            return *this;
        }
    }
}
