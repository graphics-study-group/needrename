#ifndef REFLECTION_VAR_INCLUDED
#define REFLECTION_VAR_INCLUDED

#include <string>
#include <memory>

namespace Engine
{
    namespace Reflection
    {
        class Type;

        class Var
        {
        public:
            Var() = default;
            Var(const Var &var) = default;
            template <typename T>
            Var(T &obj);
            Var(std::shared_ptr<Type> type, void *data);
            ~Var() = default;

        protected:
            void *m_data = nullptr;

        public:
            std::shared_ptr<Type> m_type = nullptr;

            void *GetDataPtr();

            template <typename T>
            T &Get();
            template <typename T>
            T &Set(const T &value);
            template <typename... Args>
            Var InvokeMethod(const std::string &name, Args...);
            Var GetMember(const std::string &name);

            Var &operator=(const Var &var);
        };

        // TODO: Implement const type
        // TODO: Implement pointer and reference type
    }
}

#include "Var.tpp"

#endif // REFLECTION_VAR_INCLUDED
