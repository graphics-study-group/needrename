#ifndef ANROREFL_METHOD_INCLUDED
#define ANROREFL_METHOD_INCLUDED

#include "Export.h"
#include "utils.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AnnoRefl {
    class Type;
    class Var;

    class ANROREFL_API Method {
    protected:
        friend class Type;
        Method() = delete;
        Method(
            const std::string &final_name,
            const WrapperMemberFunc &func,
            std::shared_ptr<const Type> return_type,
            bool is_const,
            bool return_value_needs_free
        );

    public:
        ~Method() = default;

    protected:
        WrapperMemberFunc m_func{};
        std::shared_ptr<const Type> m_return_type{};
        bool m_is_const = false;
        bool m_return_value_needs_free = false;
        std::string m_name{};

    public:
        std::shared_ptr<const Type> GetReturnType() const;
        bool IsConst() const;
        bool ReturnValueNeedsFree() const;
        const std::string &GetName() const;

        template <typename... Args>
        Var Invoke(Var &obj, Args &&...) const;
        template <typename... Args>
        Var Invoke(void *obj, Args &&...) const;
    };
} // namespace AnnoRefl

#include "Method.tpp"

#endif // ANROREFL_METHOD_INCLUDED
