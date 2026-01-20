#include "Method.h"
#include "Type.h"
#include "Var.h"
#include <stdexcept>

namespace Engine {
    namespace Reflection {
        template <typename T>
        T &Var::Get() {
            if constexpr (std::is_reference_v<T>) return *static_cast<std::remove_reference_t<T> *>(m_data);
            else return *static_cast<T *>(m_data);
        }

        template <typename T>
        std::shared_ptr<T> Var::GetAsSharedPtr() {
            if (m_type->GetTypeKind() == Type::TypeKind::Const && !std::is_const_v<T>)
                throw std::runtime_error("Cannot get non-const shared pointer from const Var");
            MarkNeedFree(false);
            return std::shared_ptr<T>(static_cast<T *>(m_data));
        }

        template <typename T>
        T &Var::Set(const T &value) {
            if (m_type->GetTypeKind() == Type::TypeKind::Const)
                throw std::runtime_error("Cannot set value of a const Var");
            if constexpr (std::is_reference_v<T>) return *static_cast<std::remove_reference_t<T> *>(m_data) = value;
            else return *static_cast<T *>(m_data) = value;
        }

        template <typename... Args>
        Var Var::InvokeMethod(const std::string &name, Args &&...args) {
            std::shared_ptr<const Method> method;
            if (m_type->GetTypeKind() == Type::TypeKind::Const) {
                method = std::dynamic_pointer_cast<const ConstType>(m_type)->m_base_type->GetMethod(
                    name, std::forward<Args>(args)...
                );
                if (method->IsConst() == false)
                    throw std::runtime_error("Method " + name + " is not const, but the Var is const");
            } else {
                method = m_type->GetMethod(name, std::forward<Args>(args)...);
            }
            if (!method) throw std::runtime_error("Method not found");
            return method->Invoke(m_data, std::forward<Args>(args)...);
        }

        template <typename T>
        void ArrayVar::Append(const T &value) {
            size_t new_size = GetSize() + 1;
            m_field->ResizeArray(m_data, new_size);
            GetElement(new_size - 1).Set<T>(value);
        }
    } // namespace Reflection
} // namespace Engine
