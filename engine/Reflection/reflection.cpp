#include "reflection.h"
#include "Type.h"

#include "meta_engine/reflection_init.ipp"

namespace Engine
{
    namespace Reflection
    {
        void Initialize()
        {
            Registrar::RegisterBasicTypes();
            RegisterAllTypes();
        }

        /// @brief Register basic types list in https://en.cppreference.com/w/cpp/language/type
        void Registrar::RegisterBasicTypes()
        {
            Type::s_type_map["void"] = Type::s_type_map[typeid(void).name()] = std::shared_ptr<Type>(new Type("void", &typeid(void), false));
            Type::s_type_map["std::nullptr_t"] = Type::s_type_map[typeid(std::nullptr_t).name()] = std::shared_ptr<Type>(new Type("std::nullptr_t", &typeid(std::nullptr_t), false));
            Type::s_type_map["bool"] = Type::s_type_map[typeid(bool).name()] = std::shared_ptr<Type>(new Type("bool", &typeid(bool), false));
            Type::s_type_map["char"] = Type::s_type_map[typeid(char).name()] = std::shared_ptr<Type>(new Type("char", &typeid(char), false));
            Type::s_type_map["signed char"] = Type::s_type_map[typeid(signed char).name()] = std::shared_ptr<Type>(new Type("signed char", &typeid(signed char), false));
            Type::s_type_map["unsigned char"] = Type::s_type_map[typeid(unsigned char).name()] = std::shared_ptr<Type>(new Type("unsigned char", &typeid(unsigned char), false));
            Type::s_type_map["char8_t"] = Type::s_type_map[typeid(char8_t).name()] = std::shared_ptr<Type>(new Type("char8_t", &typeid(char8_t), false));
            Type::s_type_map["char16_t"] = Type::s_type_map[typeid(char16_t).name()] = std::shared_ptr<Type>(new Type("char16_t", &typeid(char16_t), false));
            Type::s_type_map["char32_t"] = Type::s_type_map[typeid(char32_t).name()] = std::shared_ptr<Type>(new Type("char32_t", &typeid(char32_t), false));
            Type::s_type_map["wchar_t"] = Type::s_type_map[typeid(wchar_t).name()] = std::shared_ptr<Type>(new Type("wchar_t", &typeid(wchar_t), false));
            Type::s_type_map["short"] = Type::s_type_map[typeid(short).name()] = std::shared_ptr<Type>(new Type("short", &typeid(short), false));
            Type::s_type_map["unsigned short"] = Type::s_type_map[typeid(unsigned short).name()] = std::shared_ptr<Type>(new Type("unsigned short", &typeid(unsigned short), false));
            Type::s_type_map["int"] = Type::s_type_map[typeid(int).name()] = std::shared_ptr<Type>(new Type("int", &typeid(int), false));
            Type::s_type_map["unsigned int"] = Type::s_type_map[typeid(unsigned int).name()] = std::shared_ptr<Type>(new Type("unsigned int", &typeid(unsigned int), false));
            Type::s_type_map["long"] = Type::s_type_map[typeid(long).name()] = std::shared_ptr<Type>(new Type("long", &typeid(long), false));
            Type::s_type_map["unsigned long"] = Type::s_type_map[typeid(unsigned long).name()] = std::shared_ptr<Type>(new Type("unsigned long", &typeid(unsigned long), false));
            Type::s_type_map["long long"] = Type::s_type_map[typeid(long long).name()] = std::shared_ptr<Type>(new Type("long long", &typeid(long long), false));
            Type::s_type_map["unsigned long long"] = Type::s_type_map[typeid(unsigned long long).name()] = std::shared_ptr<Type>(new Type("unsigned long long", &typeid(unsigned long long), false));
            Type::s_type_map["float"] = Type::s_type_map[typeid(float).name()] = std::shared_ptr<Type>(new Type("float", &typeid(float), false));
            Type::s_type_map["double"] = Type::s_type_map[typeid(double).name()] = std::shared_ptr<Type>(new Type("double", &typeid(double), false));
            Type::s_type_map["long double"] = Type::s_type_map[typeid(long double).name()] = std::shared_ptr<Type>(new Type("long double", &typeid(long double), false));

            Type::s_type_map["int8_t"] = Type::s_type_map[typeid(int8_t).name()];
            Type::s_type_map["int16_t"] = Type::s_type_map[typeid(int16_t).name()];
            Type::s_type_map["int32_t"] = Type::s_type_map[typeid(int32_t).name()];
            Type::s_type_map["int64_t"] = Type::s_type_map[typeid(int64_t).name()];
            Type::s_type_map["uint8_t"] = Type::s_type_map[typeid(uint8_t).name()];
            Type::s_type_map["uint16_t"] = Type::s_type_map[typeid(uint16_t).name()];
            Type::s_type_map["uint32_t"] = Type::s_type_map[typeid(uint32_t).name()];
            Type::s_type_map["uint64_t"] = Type::s_type_map[typeid(uint64_t).name()];
            Type::s_type_map["int_least8_t"] = Type::s_type_map[typeid(int_least8_t).name()];
            Type::s_type_map["int_least16_t"] = Type::s_type_map[typeid(int_least16_t).name()];
            Type::s_type_map["int_least32_t"] = Type::s_type_map[typeid(int_least32_t).name()];
            Type::s_type_map["int_least64_t"] = Type::s_type_map[typeid(int_least64_t).name()];
            Type::s_type_map["uint_least8_t"] = Type::s_type_map[typeid(uint_least8_t).name()];
            Type::s_type_map["uint_least16_t"] = Type::s_type_map[typeid(uint_least16_t).name()];
            Type::s_type_map["uint_least32_t"] = Type::s_type_map[typeid(uint_least32_t).name()];
            Type::s_type_map["uint_least64_t"] = Type::s_type_map[typeid(uint_least64_t).name()];
            Type::s_type_map["int_fast8_t"] = Type::s_type_map[typeid(int_fast8_t).name()];
            Type::s_type_map["int_fast16_t"] = Type::s_type_map[typeid(int_fast16_t).name()];
            Type::s_type_map["int_fast32_t"] = Type::s_type_map[typeid(int_fast32_t).name()];
            Type::s_type_map["int_fast64_t"] = Type::s_type_map[typeid(int_fast64_t).name()];
            Type::s_type_map["uint_fast8_t"] = Type::s_type_map[typeid(uint_fast8_t).name()];
        }

        void Registrar::RegisterNewType(const std::string &name, const std::type_info *type_info, bool reflectable)
        {
            Type::s_type_map[name] = std::shared_ptr<Type>(new Type(name, type_info, reflectable));
        }

        std::shared_ptr<Type> GetType(const std::string &name, const std::type_info *type_info)
        {
            if (Type::s_type_map.find(name) != Type::s_type_map.end())
                return Type::s_type_map[name];
            Registrar::RegisterNewType(name, type_info, false);
            return Type::s_type_map[name];
        }
    }
}
