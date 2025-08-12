#ifndef REFLECTION_VAR_INCLUDED
#define REFLECTION_VAR_INCLUDED

#include <memory>
#include <string>

namespace Engine {
    namespace Reflection {
        class Type;

        class Var {
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
            Var InvokeMethod(const std::string &name, Args &&...);

            /// @brief Get a member field of the object.
            /// @param name the name of the field
            /// @return a Var object representing the field
            Var GetMember(const std::string &name);

            /// @brief Get an element of an array field of the object.
            /// @param name the name of the array field
            /// @param index the index of the element
            /// @return a Var object representing the element
            Var GetElementOfArrayMember(const std::string &name, size_t index);

            /// @brief Get the size of an array member field of the object.
            /// @param name the name of the array field
            /// @return the size of the array
            size_t GetArrayMemberSize(const std::string &name) const;

            /// @brief If the type of the Var is a pointer, get the Var it points to.
            /// @return the Var it points to
            Var GetPointedVar();

            Var &operator=(const Var &var);
        };
    } // namespace Reflection
} // namespace Engine

#include "Var.tpp"

#endif // REFLECTION_VAR_INCLUDED
