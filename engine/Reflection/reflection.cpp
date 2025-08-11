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
        void Initialize() {
            Registrar::RegisterBasicTypes();
            RegisterAllTypes();
        }

        /// @brief Register basic types list in https://en.cppreference.com/w/cpp/language/type and some frequently used
        /// types.
        void Registrar::RegisterBasicTypes() {
            Type::s_index_type_map[std::type_index(typeid(void))] =
                std::shared_ptr<const Type>(new Type("void", 0u, false));
            Type::s_index_type_map[std::type_index(typeid(std::nullptr_t))] =
                std::shared_ptr<const Type>(new Type("std::nullptr_t", sizeof(std::nullptr_t), false));
            Type::s_index_type_map[std::type_index(typeid(bool))] =
                std::shared_ptr<const Type>(new Type("bool", sizeof(bool), false));
            Type::s_index_type_map[std::type_index(typeid(char))] =
                std::shared_ptr<const Type>(new Type("char", sizeof(char), false));
            Type::s_index_type_map[std::type_index(typeid(signed char))] =
                std::shared_ptr<const Type>(new Type("signed char", sizeof(signed char), false));
            Type::s_index_type_map[std::type_index(typeid(unsigned char))] =
                std::shared_ptr<const Type>(new Type("unsigned char", sizeof(unsigned char), false));
            Type::s_index_type_map[std::type_index(typeid(char8_t))] =
                std::shared_ptr<const Type>(new Type("char8_t", sizeof(char8_t), false));
            Type::s_index_type_map[std::type_index(typeid(char16_t))] =
                std::shared_ptr<const Type>(new Type("char16_t", sizeof(char16_t), false));
            Type::s_index_type_map[std::type_index(typeid(char32_t))] =
                std::shared_ptr<const Type>(new Type("char32_t", sizeof(char32_t), false));
            Type::s_index_type_map[std::type_index(typeid(wchar_t))] =
                std::shared_ptr<const Type>(new Type("wchar_t", sizeof(wchar_t), false));
            Type::s_index_type_map[std::type_index(typeid(short))] =
                std::shared_ptr<const Type>(new Type("short", sizeof(short), false));
            Type::s_index_type_map[std::type_index(typeid(unsigned short))] =
                std::shared_ptr<const Type>(new Type("unsigned short", sizeof(unsigned short), false));
            Type::s_index_type_map[std::type_index(typeid(int))] =
                std::shared_ptr<const Type>(new Type("int", sizeof(int), false));
            Type::s_index_type_map[std::type_index(typeid(unsigned int))] =
                std::shared_ptr<const Type>(new Type("unsigned int", sizeof(unsigned int), false));
            Type::s_index_type_map[std::type_index(typeid(long))] =
                std::shared_ptr<const Type>(new Type("long", sizeof(long), false));
            Type::s_index_type_map[std::type_index(typeid(unsigned long))] =
                std::shared_ptr<const Type>(new Type("unsigned long", sizeof(unsigned long), false));
            Type::s_index_type_map[std::type_index(typeid(long long))] =
                std::shared_ptr<const Type>(new Type("long long", sizeof(long long), false));
            Type::s_index_type_map[std::type_index(typeid(unsigned long long))] =
                std::shared_ptr<const Type>(new Type("unsigned long long", sizeof(unsigned long long), false));
            Type::s_index_type_map[std::type_index(typeid(float))] =
                std::shared_ptr<const Type>(new Type("float", sizeof(float), false));
            Type::s_index_type_map[std::type_index(typeid(double))] =
                std::shared_ptr<const Type>(new Type("double", sizeof(double), false));
            Type::s_index_type_map[std::type_index(typeid(long double))] =
                std::shared_ptr<const Type>(new Type("long double", sizeof(long double), false));
            Type::s_index_type_map[std::type_index(typeid(std::string))] =
                std::shared_ptr<const Type>(new Type("std::string", sizeof(std::string), false));
            Type::s_index_type_map[std::type_index(typeid(glm::vec2))] =
                std::shared_ptr<const Type>(new Type("glm::vec2", sizeof(glm::vec2), false));
            Type::s_index_type_map[std::type_index(typeid(glm::vec3))] =
                std::shared_ptr<const Type>(new Type("glm::vec3", sizeof(glm::vec3), false));
            Type::s_index_type_map[std::type_index(typeid(glm::vec4))] =
                std::shared_ptr<const Type>(new Type("glm::vec4", sizeof(glm::vec4), false));
            Type::s_index_type_map[std::type_index(typeid(glm::quat))] =
                std::shared_ptr<const Type>(new Type("glm::quat", sizeof(glm::quat), false));

            Type::s_name_index_map.emplace("void", std::type_index(typeid(void)));
            Type::s_name_index_map.emplace("std::nullptr_t", std::type_index(typeid(std::nullptr_t)));
            Type::s_name_index_map.emplace("bool", std::type_index(typeid(bool)));
            Type::s_name_index_map.emplace("char", std::type_index(typeid(char)));
            Type::s_name_index_map.emplace("signed char", std::type_index(typeid(signed char)));
            Type::s_name_index_map.emplace("unsigned char", std::type_index(typeid(unsigned char)));
            Type::s_name_index_map.emplace("char8_t", std::type_index(typeid(char8_t)));
            Type::s_name_index_map.emplace("char16_t", std::type_index(typeid(char16_t)));
            Type::s_name_index_map.emplace("char32_t", std::type_index(typeid(char32_t)));
            Type::s_name_index_map.emplace("wchar_t", std::type_index(typeid(wchar_t)));
            Type::s_name_index_map.emplace("short", std::type_index(typeid(short)));
            Type::s_name_index_map.emplace("unsigned short", std::type_index(typeid(unsigned short)));
            Type::s_name_index_map.emplace("int", std::type_index(typeid(int)));
            Type::s_name_index_map.emplace("unsigned int", std::type_index(typeid(unsigned int)));
            Type::s_name_index_map.emplace("long", std::type_index(typeid(long)));
            Type::s_name_index_map.emplace("unsigned long", std::type_index(typeid(unsigned long)));
            Type::s_name_index_map.emplace("long long", std::type_index(typeid(long long)));
            Type::s_name_index_map.emplace("unsigned long long", std::type_index(typeid(unsigned long long)));
            Type::s_name_index_map.emplace("float", std::type_index(typeid(float)));
            Type::s_name_index_map.emplace("double", std::type_index(typeid(double)));
            Type::s_name_index_map.emplace("long double", std::type_index(typeid(long double)));
            Type::s_name_index_map.emplace("std::string", std::type_index(typeid(std::string)));
            Type::s_name_index_map.emplace("glm::vec2", std::type_index(typeid(glm::vec2)));
            Type::s_name_index_map.emplace("glm::vec3", std::type_index(typeid(glm::vec3)));
            Type::s_name_index_map.emplace("glm::vec4", std::type_index(typeid(glm::vec4)));
            Type::s_name_index_map.emplace("glm::quat", std::type_index(typeid(glm::quat)));

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
                assert(Type::s_index_type_map[type_index]->m_reflectable == reflectable);
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
