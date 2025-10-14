#include "Var.h"
#include "Field.h"
#include "Type.h"
#include <cstring>
#include <stdexcept>

namespace Engine {
    namespace Reflection {
        Var::Var(std::shared_ptr<const Type> type, void *data) : m_data(data), m_type(type) {
        }

        Var::Var(Var &&var) {
            m_data = var.m_data;
            m_type = var.m_type;
            m_need_free = var.m_need_free;
            var.Reset();
        }

        Var &Var::operator=(Var &&var) {
            if (this != &var) {
                Reset();
                m_data = var.m_data;
                m_type = var.m_type;
                m_need_free = var.m_need_free;
                var.Reset();
            }
            return *this;
        }

        Var::~Var() {
            Reset();
        }

        void *Var::GetDataPtr() {
            return m_data;
        }

        void Var::Copy(const Var &var) {
            memcpy(m_data, var.m_data, m_type->GetTypeSize());
        }

        void Var::Reset() {
            /// XXX: Don't know how to call destructor of the type
            if (m_need_free && m_data) {
                free(m_data);
            }
            m_data = nullptr;
            m_need_free = false;
            m_type = nullptr;
        }

        void Var::MarkNeedFree() {
            m_need_free = true;
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

        std::string_view Var::GetEnumString() const {
            if (m_type->GetTypeKind() != Type::TypeKind::Enum) {
                throw std::runtime_error("Var is not an enum type");
            }
            auto type = std::static_pointer_cast<const EnumType>(m_type);
            switch (m_type->GetTypeSize()) {
            case 1:
                return type->to_string(*(static_cast<uint8_t *>(m_data)));
            case 2:
                return type->to_string(*(static_cast<uint16_t *>(m_data)));
            case 4:
                return type->to_string(*(static_cast<uint32_t *>(m_data)));
            case 8:
                return type->to_string(*(static_cast<uint64_t *>(m_data)));
            default:
                throw std::runtime_error("Unsupported enum size");
            }
        }

        void Var::SetEnumFromString(std::string_view sv) {
            if (m_type->GetTypeKind() != Type::TypeKind::Enum) {
                throw std::runtime_error("Var is not an enum type");
            }
            auto type = std::static_pointer_cast<const EnumType>(m_type);
            switch (m_type->GetTypeSize()) {
            case 1: {
                *(static_cast<uint8_t *>(m_data)) = static_cast<uint8_t>(*type->from_string(sv));
                break;
            }
            case 2: {
                *(static_cast<uint16_t *>(m_data)) = static_cast<uint16_t>(*type->from_string(sv));
                break;
            }
            case 4: {
                *(static_cast<uint32_t *>(m_data)) = static_cast<uint32_t>(*type->from_string(sv));
                break;
            }
            case 8: {
                *(static_cast<uint64_t *>(m_data)) = static_cast<uint64_t>(*type->from_string(sv));
                break;
            }
            default:
                throw std::runtime_error("Unsupported enum size");
            }
        }

        std::shared_ptr<const Type> Var::GetType() const {
            return m_type;
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
                ret = std::move(ret.GetConstVar());
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
