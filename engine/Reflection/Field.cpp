#include "Field.h"
#include "Var.h"
#include <cassert>
#include <stdexcept>

namespace Engine {
    namespace Reflection {
        Field::Field(
            const std::string &name,
            std::weak_ptr<Type> classtype,
            std::shared_ptr<const Type> fieldtype,
            const WrapperFieldFunc &getter_func
        ) : m_getter(getter_func), m_name(name), m_classtype(classtype), m_fieldtype(fieldtype) {
            assert(classtype.expired() == false);
            assert(fieldtype);
        }

        Var Field::GetVar(Var &obj) const {
            if (obj.m_type != m_classtype.lock()) throw std::runtime_error("Invalid object type");
            void *data = nullptr;
            m_getter(obj.GetDataPtr(), data);
            return Var(m_fieldtype, data);
        }

        Var Field::GetVar(void *obj) const {
            void *data = nullptr;
            m_getter(obj, data);
            return Var(m_fieldtype, data);
        }

        ArrayField::ArrayField(
            const std::string &name,
            std::weak_ptr<Type> classtype,
            std::shared_ptr<const Type> element_type,
            const WrapperArrayFieldFunc &getter_func,
            const WrapperArrayFieldSize &size_getter_func
        ) :
            m_getter(getter_func), m_size_getter(size_getter_func), m_name(name), m_classtype(classtype),
            m_element_type(element_type) {
            assert(classtype.expired() == false);
            assert(element_type);
        }

        Var ArrayField::GetElementVar(Var &obj, size_t index) const {
            if (obj.m_type != m_classtype.lock()) throw std::runtime_error("Invalid object type");
            void *data = nullptr;
            m_getter(obj.GetDataPtr(), index, data);
            return Var(m_element_type, data);
        }

        Var ArrayField::GetElementVar(void *obj, size_t index) const {
            void *data = nullptr;
            m_getter(obj, index, data);
            return Var(m_element_type, data);
        }

        size_t ArrayField::GetArraySize(Var &obj) const {
            size_t ret = 0u;
            m_size_getter(obj.GetDataPtr(), ret);
            return ret;
        }

        size_t ArrayField::GetArraySize(void *obj) const {
            size_t ret = 0u;
            m_size_getter(obj, ret);
            return ret;
        }
    } // namespace Reflection
} // namespace Engine
