#include "reflection.h"
#include "Type.h"
#include "meta_engine/reflection_init.ipp"
#include <cassert>
#include <cstdint>
#include <glm.hpp>
#include <gtc/quaternion.hpp>
#include <string>

namespace Engine {
    namespace Reflection {
        template <typename T>
        static void InitBuiltinType(const char* name) {
            Type *type = new Type(name, sizeof(T), false);
            type->SetDeleter([](void *obj) {
                delete static_cast<std::add_pointer_t<T>>(obj);
            });
            Type::s_index_type_map[std::type_index(typeid(T))] = std::shared_ptr<const Type>(type);
            PointerType::RegisterSmartPointerGetFunc<T>();
            Type::s_name_index_map.emplace(name, std::type_index(typeid(T)));
        }

        void Initialize() {
            Registrar::RegisterBasicTypes();
            RegisterAllTypes();
        }

        /// @brief Register basic types list in https://en.cppreference.com/w/cpp/language/type and some frequently used
        /// types.
        void Registrar::RegisterBasicTypes() {
            // special-case void (cannot use InitBuiltinType<void>())
            Type::s_index_type_map[std::type_index(typeid(void))] =
                std::shared_ptr<const Type>(new Type("void", 0u, false));
            PointerType::RegisterSmartPointerGetFunc<void>();
            Type::s_name_index_map.emplace("void", std::type_index(typeid(void)));

            // use InitBuiltinType<T>("T") for the rest
            InitBuiltinType<std::nullptr_t>("std::nullptr_t");
            InitBuiltinType<bool>("bool");
            InitBuiltinType<char>("char");
            InitBuiltinType<signed char>("signed char");
            InitBuiltinType<unsigned char>("unsigned char");
            InitBuiltinType<char8_t>("char8_t");
            InitBuiltinType<char16_t>("char16_t");
            InitBuiltinType<char32_t>("char32_t");
            InitBuiltinType<wchar_t>("wchar_t");
            InitBuiltinType<short>("short");
            InitBuiltinType<unsigned short>("unsigned short");
            InitBuiltinType<int>("int");
            InitBuiltinType<unsigned int>("unsigned int");
            InitBuiltinType<long>("long");
            InitBuiltinType<unsigned long>("unsigned long");
            InitBuiltinType<long long>("long long");
            InitBuiltinType<unsigned long long>("unsigned long long");
            InitBuiltinType<float>("float");
            InitBuiltinType<double>("double");
            InitBuiltinType<long double>("long double");
            InitBuiltinType<std::string>("std::string");
            InitBuiltinType<glm::vec2>("glm::vec2");
            InitBuiltinType<glm::vec3>("glm::vec3");
            InitBuiltinType<glm::vec4>("glm::vec4");
            InitBuiltinType<glm::quat>("glm::quat");

            Type::s_name_index_map.emplace("int8_t", std::type_index(typeid(int8_t)));
            Type::s_name_index_map.emplace("int16_t", std::type_index(typeid(int16_t)));
            Type::s_name_index_map.emplace("int32_t", std::type_index(typeid(int32_t)));
            Type::s_name_index_map.emplace("int64_t", std::type_index(typeid(int64_t)));
            Type::s_name_index_map.emplace("uint8_t", std::type_index(typeid(uint8_t)));
            Type::s_name_index_map.emplace("uint16_t", std::type_index(typeid(uint16_t)));
            Type::s_name_index_map.emplace("uint32_t", std::type_index(typeid(uint32_t)));
            Type::s_name_index_map.emplace("uint64_t", std::type_index(typeid(uint64_t)));
            Type::s_name_index_map.emplace("int_least8_t", std::type_index(typeid(int_least8_t)));
            Type::s_name_index_map.emplace("int_least16_t", std::type_index(typeid(int_least16_t)));
            Type::s_name_index_map.emplace("int_least32_t", std::type_index(typeid(int_least32_t)));
            Type::s_name_index_map.emplace("int_least64_t", std::type_index(typeid(int_least64_t)));
            Type::s_name_index_map.emplace("uint_least8_t", std::type_index(typeid(uint_least8_t)));
            Type::s_name_index_map.emplace("uint_least16_t", std::type_index(typeid(uint_least16_t)));
            Type::s_name_index_map.emplace("uint_least32_t", std::type_index(typeid(uint_least32_t)));
            Type::s_name_index_map.emplace("uint_least64_t", std::type_index(typeid(uint_least64_t)));
            Type::s_name_index_map.emplace("int_fast8_t", std::type_index(typeid(int_fast8_t)));
            Type::s_name_index_map.emplace("int_fast16_t", std::type_index(typeid(int_fast16_t)));
            Type::s_name_index_map.emplace("int_fast32_t", std::type_index(typeid(int_fast32_t)));
            Type::s_name_index_map.emplace("int_fast64_t", std::type_index(typeid(int_fast64_t)));
            Type::s_name_index_map.emplace("uint_fast8_t", std::type_index(typeid(uint_fast8_t)));
            Type::s_name_index_map.emplace("uint_fast16_t", std::type_index(typeid(uint_fast16_t)));
            Type::s_name_index_map.emplace("uint_fast32_t", std::type_index(typeid(uint_fast32_t)));
            Type::s_name_index_map.emplace("uint_fast64_t", std::type_index(typeid(uint_fast64_t)));
            Type::s_name_index_map.emplace("intmax_t", std::type_index(typeid(intmax_t)));
            Type::s_name_index_map.emplace("intptr_t", std::type_index(typeid(intptr_t)));
            Type::s_name_index_map.emplace("uintmax_t", std::type_index(typeid(uintmax_t)));
            Type::s_name_index_map.emplace("uintptr_t", std::type_index(typeid(uintptr_t)));
        }

        void Registrar::RegisterNewType(const std::string &name, std::type_index type_index, bool reflectable) {
            if (Type::s_index_type_map.find(type_index) != Type::s_index_type_map.end()) {
                assert(Type::s_index_type_map[type_index]->IsReflectable() == reflectable);
            } else {
                Type::s_index_type_map[type_index] = std::shared_ptr<const Type>(new Type(name, reflectable));
            }
            assert(Type::s_name_index_map.find(name) == Type::s_name_index_map.end());
            Type::s_name_index_map.emplace(name, type_index);
        }

        std::shared_ptr<const Type> GetType(const char *name) {
            return GetType(std::string(name));
        }

        std::shared_ptr<const Type> GetType(const std::string &name) {
            auto it = Type::s_name_index_map.find(name);
            if (it != Type::s_name_index_map.end()) return Type::s_index_type_map[it->second];
            return nullptr;
        }
    } // namespace Reflection
} // namespace Engine
