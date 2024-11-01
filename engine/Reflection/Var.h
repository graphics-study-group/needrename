#ifndef REFLECTION_VAR_INCLUDED
#define REFLECTION_VAR_INCLUDED

#include <string>
#include <memory>

namespace Engine
{
    namespace Reflection
    {
        class Type;
        class ConstVar;

        class Var
        {
        public:
            Var() = default;
            Var(const Var &var) = default;
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
            Var InvokeMethod(const std::string &name, Args&&...);
            Var GetMember(const std::string &name);

            Var &operator=(const Var &var);

            operator ConstVar() const;
        };

        class ConstVar
        {
        public:
            ConstVar() = default;
            ConstVar(const ConstVar &var) = default;
            ConstVar(std::shared_ptr<Type> type, const void *data);
            ~ConstVar() = default;

        protected:
            const void *m_data = nullptr;

        public:
            std::shared_ptr<Type> m_type = nullptr;

            const void *GetDataPtr() const;

            template <typename T>
            const T &Get() const;
            template <typename T>
            const T &Set(const T &value) const;
            template <typename... Args>
            ConstVar InvokeMethod(const std::string &name, Args&&...);
            ConstVar GetMember(const std::string &name);

            ConstVar &operator=(const ConstVar &var);
        };

        // TODO: Implement const type
        // TODO: Implement pointer and reference type
    }
}

#include "Var.tpp"

#endif // REFLECTION_VAR_INCLUDED
