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
            Var(Var &&var);
            Var &operator=(Var &&var);
            Var(const Var &var) = delete;
            Var &operator=(const Var &var) = delete;
            Var(std::shared_ptr<const Type> type, void *data);
            ~Var();

        protected:
            void *m_data = nullptr;
            bool m_need_free = false;
            std::shared_ptr<const Type> m_type = nullptr;

        public:
            // Get the void pointer to the data
            void *GetDataPtr();

            /// @brief Get the data of the Var as type T.
            /// @return a reference to the data of type T
            template <typename T>
            T &Get();

            /// @brief Get the data of the Var as a shared pointer of type T. The var will be marked as don't need free.
            /// @tparam T The type of the shared pointer
            /// @return The shared pointer of type T
            template <typename T>
            std::shared_ptr<T> GetAsSharedPtr();

            /// @brief Set the data of the Var as type T.
            /// @param value the value to set
            /// @return a reference to the data of type T
            template <typename T>
            T &Set(const T &value);

            /// @brief Copy the data from another Var.
            /// @param var the Var to copy from
            void Copy(const Var &var);

            /// @brief Reset the Var to its default state. May free the data.
            void Reset();

            /// @brief Mark the Var as needing to be freed.
            void MarkNeedFree(bool need_free = true);

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

            /// @brief Get a const version of the Var.
            /// @return a Var object representing the const version
            Var GetConstVar();

            /// @brief If the type of the Var is an enum, get the enum value as a string.
            /// @return the enum value as a string
            std::string_view GetEnumString() const;

            /// @brief If the type of the Var is an enum, set the enum value from a string.
            /// @param sv the string to set the enum value from
            void SetEnumFromString(std::string_view sv);

            std::shared_ptr<const Type> GetType() const;
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
