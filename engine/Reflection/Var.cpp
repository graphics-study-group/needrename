#include "Var.h"
#include "Field.h"
#include "Type.h"
#include <cstring>
#include <stdexcept>

namespace Engine {
    namespace Reflection {
        Var::Var(std::shared_ptr<const Type> type, void *data) : m_data(data), m_type(type) {
        }

        void *Var::GetDataPtr() {
            return m_data;
        }

        void Var::Copy(const Var &var) {
            memcpy(m_data, var.m_data, m_type->GetTypeSize());
        }

        Var Var::GetMember(const std::string &name) {
            std::shared_ptr<const Field> field;
            if (m_type->GetTypeKind() == Type::TypeKind::Const) {
                field = std::dynamic_pointer_cast<const ConstType>(m_type)->m_base_type->GetField(name);
            } else {
                field = m_type->GetField(name);
            }
            if (!field) throw std::runtime_error("Field not found");
            auto ret = field->GetVar(m_data);
            if (m_type->GetTypeKind() == Type::TypeKind::Const) {
                ret.m_type = std::shared_ptr<const Type>(new ConstType(ret.m_type));
            }
            return ret;
        }

        ArrayVar Var::GetArrayMember(const std::string &name) {
            std::shared_ptr<const ArrayField> array_field;
            bool is_const = false;
            if (m_type->GetTypeKind() == Type::TypeKind::Const) {
                array_field = std::dynamic_pointer_cast<const ConstType>(m_type)->m_base_type->GetArrayField(name);
                is_const = true;
            } else {
                array_field = m_type->GetArrayField(name);
            }
            if (!array_field) throw std::runtime_error("Array field not found");
            return ArrayVar(array_field, m_data, is_const);
        }

        Var Var::GetPointedVar() {
            if (m_type->GetTypeKind() != Type::TypeKind::Pointer) {
                throw std::runtime_error("Var is not a pointer type");
            }
            auto type = std::static_pointer_cast<const PointerType>(m_type);
            if (type->GetPointerTypeKind() == PointerType::PointerTypeKind::Raw) {
                return Var(type->GetPointedType(), *static_cast<void **>(m_data));
            } else {
                auto type_index_it = Type::s_name_index_map.find(type->GetPointedType()->GetName());
                if (type_index_it == Type::s_name_index_map.end()) {
                    throw std::runtime_error("Type not found");
                }
                switch (type->GetPointerTypeKind()) {
                case PointerType::PointerTypeKind::Shared: {
                    return Var(
                        type->GetPointedType(),
                        PointerType::s_shared_pointer_getter_map.at(type_index_it->second)(m_data)
                    );
                }
                case PointerType::PointerTypeKind::Weak: {
                    return Var(
                        type->GetPointedType(), PointerType::s_weak_pointer_getter_map.at(type_index_it->second)(m_data)
                    );
                }
                case PointerType::PointerTypeKind::Unique: {
                    return Var(
                        type->GetPointedType(),
                        PointerType::s_unique_pointer_getter_map.at(type_index_it->second)(m_data)
                    );
                }
                default:
                    throw std::runtime_error("Not Implemented");
                }
            }
        }

        Var Var::GetConstVar() {
            return Var(std::shared_ptr<const Type>(new ConstType(m_type)), m_data);
        }

        std::shared_ptr<const Type> Var::GetType() const {
            return m_type;
        }

        Var &Var::operator=(const Var &var) {
            m_type = var.m_type;
            m_data = var.m_data;
            return *this;
        }

        ArrayVar::ArrayVar(std::shared_ptr<const ArrayField> field, void *data, bool is_const) :
            m_field(field), m_data(data), m_is_const(is_const) {
        }

        ArrayVar &ArrayVar::operator=(const ArrayVar &var) {
            m_field = var.m_field;
            m_data = var.m_data;
            m_is_const = var.m_is_const;
            return *this;
        }

        Var ArrayVar::GetElement(size_t index) {
            if (index >= GetSize()) throw std::runtime_error("Index out of range");
            auto ret = m_field->GetElementVar(m_data, index);
            if (m_is_const) {
                ret = ret.GetConstVar();
            }
            return ret;
        }

        size_t ArrayVar::GetSize() const {
            return m_field->GetArraySize(m_data);
        }

        void ArrayVar::Resize(size_t new_size) const {
            if (m_is_const) throw std::runtime_error("Cannot resize a const ArrayVar");
            m_field->ResizeArray(m_data, new_size);
        }

        void ArrayVar::Append(const Var &var) {
            if (m_is_const) throw std::runtime_error("Cannot append to a const ArrayVar");
            size_t new_size = GetSize() + 1;
            m_field->ResizeArray(m_data, new_size);
            GetElement(new_size - 1).Copy(var);
        }

        void ArrayVar::Remove(size_t index) {
            if (m_is_const) throw std::runtime_error("Cannot remove from a const ArrayVar");
            size_t old_size = GetSize();
            if (index >= old_size) throw std::runtime_error("Index out of range");
            for (size_t i = index; i < old_size - 1; ++i) {
                GetElement(i).Copy(GetElement(i + 1));
            }
            m_field->ResizeArray(m_data, old_size - 1);
        }
    } // namespace Reflection
} // namespace Engine
