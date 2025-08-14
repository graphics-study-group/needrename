#include "Method.h"
#include "Var.h"
#include "utils.h"

namespace Engine {
    namespace Reflection {
        template <typename... Args>
        Var Method::Invoke(Var &obj, Args &&...args) const {
            std::vector<void *> arg_pointers;
            (arg_pointers.push_back(const_cast<void *>(reinterpret_cast<const void *>(std::addressof(args)))), ...);
            void *ret = nullptr;
            m_func(obj.GetDataPtr(), ret, arg_pointers);
            auto var = Var(m_return_type, ret);
            if (m_return_value_needs_free) {
                var.MarkNeedFree();
            }
            return var;
        }

        template <typename... Args>
        Var Method::Invoke(void *obj, Args &&...args) const {
            std::vector<void *> arg_pointers;
            (arg_pointers.push_back(const_cast<void *>(reinterpret_cast<const void *>(std::addressof(args)))), ...);
            void *ret = nullptr;
            m_func(obj, ret, arg_pointers);
            auto var = Var(m_return_type, ret);
            if (m_return_value_needs_free) {
                var.MarkNeedFree();
            }
            return var;
        }
    } // namespace Reflection
} // namespace Engine
