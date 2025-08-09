#include "Var.h"
#include "Method.h"
#include "Type.h"
#include <stdexcept>

namespace Engine
{
    namespace Reflection
    {
        // TODO: do some type checking in Get and Set
        template <typename T>
        T &Var::Get()
        {
            if constexpr (std::is_reference_v<T>)
                return *static_cast<std::remove_reference_t<T> *>(m_data);
            else
                return *static_cast<T *>(m_data);
        }

        template <typename T>
        T &Var::Set(const T &value)
        {
            if constexpr (std::is_reference_v<T>)
                return *static_cast<std::remove_reference_t<T> *>(m_data) = value;
            else
                return *static_cast<T *>(m_data) = value;
        }

        template <typename... Args>
        Var Var::InvokeMethod(const std::string &name, Args&&... args)
        {
            std::shared_ptr<const Method> method = m_type->GetMethod(name, std::forward<Args>(args)...);
            if(!method)
                throw std::runtime_error("Method not found");
            return method->Invoke(m_data, std::forward<Args>(args)...);
        }
    }
}
