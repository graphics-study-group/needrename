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
            return *static_cast<T *>(m_data);
        }

        template <typename T>
        T &Var::Set(const T &value)
        {
            return *static_cast<T *>(m_data) = value;
        }

        template <typename... Args>
        Var Var::InvokeMethod(const std::string &name, Args&&... args)
        {
            std::shared_ptr<Method> method = m_type->GetMethod(name, std::forward<Args>(args)...);
            if(!method)
                throw std::runtime_error("Method not found");
            return method->Invoke(m_data, std::forward<Args>(args)...);
        }

        template <typename T>
        const T &ConstVar::Get() const
        {
            return *static_cast<const T *>(m_data);
        }

        template <typename T>
        const T &ConstVar::Set(const T &value) const
        {
            return *static_cast<const T *>(m_data) = value;
        }

        template <typename... Args>
        ConstVar ConstVar::InvokeMethod(const std::string &name, Args&&... args)
        {
            std::shared_ptr<Method> method = m_type->GetMethod(name, std::forward<Args>(args)...);
            if(!method)
                throw std::runtime_error("Method not found");
            return method->ConstInvoke(m_data, std::forward<Args>(args)...);
        }
    }
}
