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

        size_t Type::GetTypeSize() const {
            return m_size;
        }

        bool Type::IsReflectable() const {
            return m_reflectable;
        }

        Type::TypeKind Type::GetTypeKind() const {
            return m_kind;
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
            const WrapperArrayFieldSize &array_size_getter_func,
            const WrapperArrayResizeFunc &array_resize_func
        ) {
            m_array_fields[name] = std::shared_ptr<const ArrayField>(new ArrayField(
                name, shared_from_this(), element_type, array_getter_func, array_size_getter_func, array_resize_func
            ));
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

        const std::unordered_map<std::string, std::shared_ptr<const ArrayField>> &Type::GetArrayFields() const {
            return m_array_fields;
        }

        ConstType::ConstType(std::shared_ptr<const Type> base_type) :
            Type(base_type->GetName(), base_type->GetTypeSize(), false), m_base_type(base_type) {
            m_kind = TypeKind::Const;
        }

        std::unordered_map<std::type_index, WrapperSmartPointerGet> PointerType::s_shared_pointer_getter_map;
        std::unordered_map<std::type_index, WrapperSmartPointerGet> PointerType::s_weak_pointer_getter_map;
        std::unordered_map<std::type_index, WrapperSmartPointerGet> PointerType::s_unique_pointer_getter_map;

        PointerType::PointerType(std::shared_ptr<const Type> pointed_type, size_t size, PointerTypeKind kind) :
            Type(pointed_type->GetName(), size, pointed_type->IsReflectable()), m_pointed_type(pointed_type),
            m_pointer_kind(kind) {
            m_kind = TypeKind::Pointer;
            switch (kind) {
            case PointerTypeKind::Raw:
                m_name = m_pointed_type->GetName() + "*";
                break;
            case PointerTypeKind::Shared:
                m_name = "std::shared_ptr<" + m_pointed_type->GetName() + ">";
                break;
            case PointerTypeKind::Weak:
                m_name = "std::weak_ptr<" + m_pointed_type->GetName() + ">";
                break;
            case PointerTypeKind::Unique:
                m_name = "std::unique_ptr<" + m_pointed_type->GetName() + ">";
                break;
            }
        }

        std::shared_ptr<const Type> PointerType::GetPointedType() const {
            return m_pointed_type;
        }

        PointerType::PointerTypeKind PointerType::GetPointerTypeKind() const {
            return m_pointer_kind;
        }

        EnumType::EnumType(
            const std::string &name,
            size_t size,
            bool reflectable,
            const std::vector<uint64_t> &enum_values,
            WrapperEnumToString to_string,
            WrapperEnumFromString from_string
        ) : Type(name, size, reflectable), m_enum_values(enum_values), m_to_string(to_string), m_from_string(from_string) {
            m_kind = TypeKind::Enum;
        }

        std::string_view EnumType::to_string(uint64_t value) const {
            return m_to_string(value);
        }

        std::optional<uint64_t> EnumType::from_string(std::string_view name) const {
            return m_from_string(std::string(name));
        }

        const std::vector<uint64_t> &EnumType::GetEnumValues() const {
            return m_enum_values;
        }
    } // namespace Reflection
} // namespace Engine
