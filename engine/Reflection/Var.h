#ifndef REFLECTION_VAR_INCLUDED
#define REFLECTION_VAR_INCLUDED

#include <memory>
#include <string>

namespace Engine {
    namespace Reflection {
        class Type;
        class ArrayVar;

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

            /// @brief Copy the data from another Var.
            /// @param var the Var to copy from
            void Copy(const Var &var);

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

            /// @brief Get an array member field of the object.
            /// The member should be an array, std::vector or std::array.
            /// @param name the name of the array field
            /// @return an ArrayVar object representing the array field
            ArrayVar GetArrayMember(const std::string &name);

            /// @brief If the type of the Var is a pointer, get the Var it points to.
            /// @return the Var it points to
            Var GetPointedVar();

            Var &operator=(const Var &var);
        };

        class ArrayField;

        class ArrayVar {
        protected:
            friend class Var;
            ArrayVar() = delete;
            ArrayVar(std::shared_ptr<const ArrayField> field, void *data, bool is_const);

        public:
            ArrayVar(const ArrayVar &var) = default;
            ~ArrayVar() = default;
            ArrayVar &operator=(const ArrayVar &var);

        protected:
            std::shared_ptr<const ArrayField> m_field;
            void *m_data;
            bool m_is_const;

        public:
            Var GetElement(size_t index);
            size_t GetSize() const;
            void Resize(size_t new_size) const;
            void Append(const Var &var);
            template <typename T>
            void Append(const T &value);
            void Remove(size_t index);
        };
    } // namespace Reflection
} // namespace Engine

#include "Var.tpp"

#endif // REFLECTION_VAR_INCLUDED
