#include "Var.h"
#include "Method.h"
#include "Type.h"
#include <stdexcept>

namespace Engine
{
    namespace Reflection
    {
        template <typename T>
        Var::Var(T &obj)
            : m_type(GetType(obj)), m_data(reinterpret_cast<void *>(&obj))
        {
        }

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
        Var Var::InvokeMethod(const std::string &name, Args... args)
        {
            auto method = m_type->GetMethod(name, args...);
            if(!method)
                throw std::runtime_error("Method not found");
            return method->Invoke(m_data, args...);
        }
    }
}
