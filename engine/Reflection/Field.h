#ifndef REFLECTION_FIELD_INCLUDED
#define REFLECTION_FIELD_INCLUDED

#include "utils.h"
#include <cstdint>
#include <memory>

namespace Engine {
    namespace Reflection {
        class Type;
        class Var;

        class Field {
        protected:
            friend class Type;
            Field() = delete;
            Field(
                const std::string &name,
                std::weak_ptr<Type> classtype,
                std::shared_ptr<const Type> fieldtype,
                const WrapperFieldFunc &getter_func
            );

        public:
            ~Field() = default;

        protected:
            WrapperFieldFunc m_getter;

        public:
            std::string m_name;
            std::weak_ptr<Type> m_classtype;
            std::shared_ptr<const Type> m_fieldtype;

            Var GetVar(Var &obj) const;
            Var GetVar(void *obj) const;
        };

        class ArrayField {
        protected:
            friend class Type;
            ArrayField() = delete;
            ArrayField(
                const std::string &name,
                std::weak_ptr<Type> classtype,
                std::shared_ptr<const Type> fieldtype,
                const WrapperArrayFieldFunc &getter_func,
                const WrapperArrayFieldSize &size_getter_func
            );

        public:
            ~ArrayField() = default;

        protected:
            WrapperArrayFieldFunc m_getter;
            WrapperArrayFieldSize m_size_getter;

        public:
            std::string m_name;
            std::weak_ptr<Type> m_classtype;
            std::shared_ptr<const Type> m_element_type;

            Var GetElementVar(Var &obj, size_t index) const;
            Var GetElementVar(void *obj, size_t index) const;
            size_t GetArraySize(Var &obj) const;
            size_t GetArraySize(void *obj) const;
        };
    } // namespace Reflection
} // namespace Engine

#endif // REFLECTION_FIELD_INCLUDED
