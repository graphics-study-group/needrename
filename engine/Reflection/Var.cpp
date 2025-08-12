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
            std::shared_ptr<const Field> field;
            if (m_type->m_specialization == Type::Const) {
                field = std::dynamic_pointer_cast<const ConstType>(m_type)->m_base_type->GetField(name);
            } else {
                field = m_type->GetField(name);
            }
            if (!field) throw std::runtime_error("Field not found");
            auto ret = field->GetVar(m_data);
            if (m_type->m_specialization == Type::Const) {
                ret.m_type = std::shared_ptr<const Type>(new ConstType(ret.m_type));
            }
            return ret;
        }

        Var Var::GetElementOfArrayMember(const std::string &name, size_t index) {
            std::shared_ptr<const ArrayField> array_field;
            if (m_type->m_specialization == Type::Const) {
                array_field = std::dynamic_pointer_cast<const ConstType>(m_type)->m_base_type->GetArrayField(name);
            } else {
                array_field = m_type->GetArrayField(name);
            }
            if (!array_field) throw std::runtime_error("Array field not found");
            auto ret = array_field->GetElementVar(m_data, index);
            if (m_type->m_specialization == Type::Const) {
                ret.m_type = std::shared_ptr<const Type>(new ConstType(ret.m_type));
            }
            return ret;
        }

        size_t Var::GetArrayMemberSize(const std::string &name) const
        {
            std::shared_ptr<const ArrayField> array_field;
            if (m_type->m_specialization == Type::Const) {
                array_field = std::dynamic_pointer_cast<const ConstType>(m_type)->m_base_type->GetArrayField(name);
            } else {
                array_field = m_type->GetArrayField(name);
            }
            if (!array_field) throw std::runtime_error("Array field not found");
            return array_field->GetArraySize(m_data);
        }

        Var Var::GetPointedVar() {
            if (m_type->m_specialization != Type::Pointer) {
                throw std::runtime_error("Var is not a pointer type");
            }
            auto type = std::static_pointer_cast<const PointerType>(m_type);
            switch (type->m_pointer_kind) {
            case PointerType::PointerTypeKind::Raw:
                return Var(type->m_pointed_type, *static_cast<void **>(m_data));
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
