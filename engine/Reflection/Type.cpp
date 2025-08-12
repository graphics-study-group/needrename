#include "Type.h"
#include <cstdarg>

namespace Engine {
    namespace Reflection {
        std::unordered_map<std::type_index, std::shared_ptr<const Type>> Type::s_index_type_map;
        std::unordered_map<std::string, std::type_index> Type::s_name_index_map;

        Type::Type(const std::string &name, size_t size, bool reflectable) :
            m_name(name), m_size(size), m_reflectable(reflectable) {
        }

        std::shared_ptr<const Method> Type::GetMethodFromMangledName(const std::string &name) const {
            if (m_methods.find(name) != m_methods.end()) return m_methods.at(name);
            for (auto &base_type : m_base_type) {
                auto func = base_type->GetMethodFromMangledName(name);
                if (func) return func;
            }
            return nullptr;
        }

        const std::string &Type::GetName() const {
            return m_name;
        }

        void Type::AddMethod(std::shared_ptr<const Method> method) {
            if (m_methods.find(method->m_name) != m_methods.end())
                throw std::runtime_error("Method " + method->m_name + " already exists");
            m_methods[method->m_name] = method;
        }

        void Type::AddBaseType(std::shared_ptr<const Type> base_type) {
            m_base_type.push_back(base_type);
        }

        void Type::AddField(
            const std::shared_ptr<const Type> field_type, const std::string &name, const WrapperFieldFunc &field
        ) {
            m_fields[name] = std::shared_ptr<const Field>(new Field(name, shared_from_this(), field_type, field));
        }

        void Type::AddField(const std::shared_ptr<const Field> field) {
            if (m_fields.find(field->m_name) != m_fields.end())
                throw std::runtime_error("Field " + field->m_name + " already exists");
            m_fields[field->m_name] = field;
        }

        void Type::AddArrayField(
            const std::shared_ptr<const Type> element_type,
            const std::string &name,
            const WrapperArrayFieldFunc &array_getter_func,
            const WrapperArrayFieldSize &array_size_getter_func
        ) {
            m_array_fields[name] = std::shared_ptr<const ArrayField>(
                new ArrayField(name, shared_from_this(), element_type, array_getter_func, array_size_getter_func)
            );
        }

        std::shared_ptr<const Field> Type::GetField(const std::string &name) const {
            if (m_fields.find(name) != m_fields.end()) return m_fields.at(name);
            for (auto &base_type : m_base_type) {
                auto field = base_type->GetField(name);
                if (field) return field;
            }
            return nullptr;
        }

        std::shared_ptr<const ArrayField> Type::GetArrayField(const std::string &name) const {
            if (m_array_fields.find(name) != m_array_fields.end()) return m_array_fields.at(name);
            for (auto &base_type : m_base_type) {
                auto field = base_type->GetArrayField(name);
                if (field) return field;
            }
            return nullptr;
        }

        const std::unordered_map<std::string, std::shared_ptr<const Field>> &Type::GetFields() const {
            return m_fields;
        }

        ConstType::ConstType(std::shared_ptr<const Type> base_type) :
            Type(base_type->m_name, base_type->m_size, base_type->m_reflectable), m_base_type(base_type) {
            m_specialization = Const;
        }

        PointerType::PointerType(std::shared_ptr<const Type> pointed_type, size_t size, PointerTypeKind kind) :
            Type(pointed_type->m_name, size, false), m_pointed_type(pointed_type), m_pointer_kind(kind) {
            m_specialization = Pointer;
        }
    } // namespace Reflection
} // namespace Engine
