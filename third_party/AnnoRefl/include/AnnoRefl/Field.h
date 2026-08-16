#ifndef ANROREFL_FIELD_INCLUDED
#define ANROREFL_FIELD_INCLUDED

#include "Export.h"
#include "utils.h"
#include <cstdint>
#include <memory>

namespace AnnoRefl {
    class Type;
    class Var;

    class ANROREFL_API Field {
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
        std::string m_name;
        std::weak_ptr<Type> m_classtype;
        std::shared_ptr<const Type> m_fieldtype;

    public:
        const std::string &GetName() const;
        const std::shared_ptr<const Type> &GetFieldType() const;
        Var GetVar(void *obj) const;
    };

    class ANROREFL_API ArrayField {
    protected:
        friend class Type;
        ArrayField() = delete;
        ArrayField(
            const std::string &name,
            std::weak_ptr<Type> classtype,
            std::shared_ptr<const Type> fieldtype,
            const WrapperArrayFieldFunc &getter_func,
            const WrapperArrayFieldSize &size_getter_func,
            const WrapperArrayResizeFunc &resize_func
        );

    public:
        ~ArrayField() = default;

    protected:
        WrapperArrayFieldFunc m_getter;
        WrapperArrayFieldSize m_size_getter;
        WrapperArrayResizeFunc m_resize_func;
        std::string m_name;
        std::weak_ptr<Type> m_classtype;
        std::shared_ptr<const Type> m_element_type;

    public:
        const std::string &GetName() const;
        const std::shared_ptr<const Type> &GetElementType() const;
        Var GetElementVar(void *obj, size_t index) const;
        size_t GetArraySize(void *obj) const;
        void ResizeArray(void *obj, size_t new_size) const;
    };
} // namespace AnnoRefl

#endif // ANROREFL_FIELD_INCLUDED
