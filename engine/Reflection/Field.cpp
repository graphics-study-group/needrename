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
            const WrapperArrayFieldSize &size_getter_func,
            const WrapperArrayResizeFunc &resize_func
        ) :
            m_getter(getter_func), m_size_getter(size_getter_func), m_resize_func(resize_func), m_name(name),
            m_classtype(classtype), m_element_type(element_type) {
            assert(classtype.expired() == false);
            assert(element_type);
        }

        Var ArrayField::GetElementVar(void *obj, size_t index) const {
            void *data = nullptr;
            m_getter(obj, index, data);
            return Var(m_element_type, data);
        }

        size_t ArrayField::GetArraySize(void *obj) const {
            size_t ret = 0u;
            m_size_getter(obj, ret);
            return ret;
        }

        void ArrayField::ResizeArray(void *obj, size_t new_size) const {
            if (m_resize_func == nullptr) {
                throw std::runtime_error("Array field " + m_name + " is not resizable");
            }
            m_resize_func(obj, new_size);
        }
    } // namespace Reflection
} // namespace Engine
