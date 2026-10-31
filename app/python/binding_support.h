#ifndef NEEDRENAME_BINDINGS_BINDING_SUPPORT_H
#define NEEDRENAME_BINDINGS_BINDING_SUPPORT_H

#include <nanobind/nanobind.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nb = nanobind;

namespace NeedrenameBindings {
    /**
     * @brief Binds a plain C++ value struct the way Python users expect.
     *
     * Every field is a read/write attribute, and the struct also accepts
     * keyword arguments that start from the C++ defaults and then apply the
     * supplied fields. The defaults come from the struct's own member
     * initializers, so the Python and C++ defaults cannot drift apart.
     *
     * @note Only use this for non-polymorphic value structs: construction uses
     * a placement-new `__init__` on storage sized for the struct itself.
     */
    template <typename T>
    class AggregateBinding {
    public:
        AggregateBinding(nb::module_ &module, const char *name) :
            m_class(module, name), m_fields(std::make_shared<Fields>()) {
            m_class.def("__init__", [fields = m_fields, name](T *self, nb::kwargs kwargs) {
                new (self) T{};
                for (auto [key, value] : kwargs) {
                    const std::string field = nb::cast<std::string>(key);
                    bool matched = false;
                    for (const auto &[field_name, set_field] : *fields) {
                        if (field_name == field) {
                            set_field(*self, value);
                            matched = true;
                            break;
                        }
                    }
                    if (!matched) {
                        const std::string message =
                            std::string(name) + "() got an unexpected keyword argument '" + field + "'";
                        throw nb::attribute_error(message.c_str());
                    }
                }
            });
        }

        /// @brief Bind a field member: read/write attribute plus keyword support.
        template <typename Member>
        AggregateBinding &field(const char *name, Member T::*member) {
            m_class.def_rw(name, member);
            m_fields->emplace_back(name, [member](T &self, nb::handle value) {
                self.*member = nb::cast<Member>(value);
            });
            return *this;
        }

    private:
        using SetField = std::function<void(T &, nb::handle)>;
        using Fields = std::vector<std::pair<std::string, SetField>>;

        nb::class_<T> m_class;
        std::shared_ptr<Fields> m_fields;
    };

    void RegisterGlmTypes(nb::module_ &m);
    void RegisterValueTypes(nb::module_ &m);
    void RegisterPhysicsApp(nb::module_ &m);
} // namespace NeedrenameBindings

#endif // NEEDRENAME_BINDINGS_BINDING_SUPPORT_H
