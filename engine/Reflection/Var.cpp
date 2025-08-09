#include "Var.h"
#include "Field.h"
#include "Type.h"
#include <stdexcept>

namespace Engine {
    namespace Reflection {
        Var::Var(std::shared_ptr<const Type> type, void *data) : m_data(data), m_type(type) {
        }

        void *Var::GetDataPtr() {
            return m_data;
        }

        Var Var::GetMember(const std::string &name) {
            auto field = m_type->GetField(name);
            if (!field) throw std::runtime_error("Field not found");
            return field->GetVar(m_data);
        }
        
        Var Var::GetPointedVar() {
            if (m_type->m_specialization != Type::Pointer) {
                throw std::runtime_error("Var is not a pointer type");
            }
            auto type = std::static_pointer_cast<const PointerType>(m_type);
            switch(type->m_pointer_kind)
            {
                case PointerType::PointerTypeKind::Raw:
                    return Var(type->m_pointed_type, *static_cast<void **>(m_data));
                default:
                    throw std::runtime_error("Not Implemented");
            }
        }

        Var Var::GetArrayElement(size_t index) {
            if (m_type->m_specialization != Type::Array) {
                throw std::runtime_error("Var is not an array type");
            }
            auto type = std::static_pointer_cast<const ArrayType>(m_type);
            switch(type->m_array_kind)
            {
                default:
                    throw std::runtime_error("Not Implemented");
            }
        }

        Var &Var::operator=(const Var &var) {
            m_type = var.m_type;
            m_data = var.m_data;
            return *this;
        }
    } // namespace Reflection
} // namespace Engine
