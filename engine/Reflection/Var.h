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
            Var(std::shared_ptr<const Type> type, void *data);
            ~Var() = default;

        protected:
            void *m_data = nullptr;

        public:
            std::shared_ptr<const Type> m_type = nullptr;

            // Get the void pointer to the data
            void *GetDataPtr();

            /// @brief Get the data of the Var as type T.
            /// @return a reference to the data of type T
            template <typename T>
            T &Get();

            /// @brief Set the data of the Var as type T.
            /// @param value the value to set
            /// @return a reference to the data of type T
            template <typename T>
            T &Set(const T &value);

            /// @brief Invoke a method of the object.
            /// @param name The name of the method
            /// @param ... the arguments to the method
            /// @return a Var object representing the return value of the method
            template <typename... Args>
            Var InvokeMethod(const std::string &name, Args&&...);

            /// @brief Get a member field of the object.
            /// @param name the name of the field
            /// @return a Var object representing the field
            Var GetMember(const std::string &name);

            Var &operator=(const Var &var);

            operator ConstVar() const;
        };

        class ConstVar
        {
        public:
            ConstVar() = default;
            ConstVar(const ConstVar &var) = default;
            ConstVar(std::shared_ptr<const Type> type, const void *data);
            ~ConstVar() = default;

        protected:
            const void *m_data = nullptr;

        public:
            std::shared_ptr<const Type> m_type = nullptr;

            // Get the const void pointer to the data
            const void *GetDataPtr() const;

            /// @brief Get the data of the ConstVar as type T.
            /// @return a const reference to the data of type T
            template <typename T>
            const T &Get() const;

            /// @brief Set the data of the ConstVar as type T.
            /// @param value the value to set
            /// @return a const reference to the data of type T
            template <typename T>
            const T &Set(const T &value) const;

            /// @brief Invoke a method of the object. Only const methods can be invoked.
            /// @param name The name of the method
            /// @param ... the arguments to the method
            /// @return a Var object representing the return value of the method
            template <typename... Args>
            Var InvokeMethod(const std::string &name, Args&&...);

            /// @brief Get a member field of the object.
            /// @param name the name of the field
            /// @return a ConstVar object representing the field
            ConstVar GetMember(const std::string &name);

            ConstVar &operator=(const ConstVar &var);
        };
    }
}

#include "Var.tpp"

#endif // REFLECTION_VAR_INCLUDED
