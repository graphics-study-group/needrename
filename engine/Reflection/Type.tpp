#include "Type.h"
#include "Field.h"
#include "Var.h"
#include "Method.h"

namespace Engine
{
    namespace Reflection
    {
        template <typename T>
        void Type::AddField(const std::shared_ptr<Type> field_type, const std::string &name, T field)
        {
            static_assert(std::is_member_pointer_v<T>);
            m_fields[name] = std::make_shared<Field>(shared_from_this(), field_type, *reinterpret_cast<std::uintptr_t *>(&field));
        }

        template <typename... Args>
        Var Type::CreateInstance(Args... args)
        {
            /// TODO: implement name mangling
            auto constructor = GetMethod(constructer_name, args...);
            if (!constructor)
                throw std::runtime_error("Constructor not found");
            return constructor->Invoke(nullptr, args...);
        }

        template <typename... Args>
        std::shared_ptr<Method> Type::GetMethod(const std::string &name, Args... args)
        {
            // TODO: implement name mangling
            return GetMethodFromManagedName(name);
        }
    }
}
