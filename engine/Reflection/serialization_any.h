#ifndef REFLECTION_SERIALIZATION_ANY_INCLUDED
#define REFLECTION_SERIALIZATION_ANY_INCLUDED

#include <any>
#include "serialization.h"

namespace Engine
{
    namespace Serialization
    {
        /// @brief concept to check if a type is std::any
        template <typename T>
        concept is_std_any = std::is_same<T, std::any>::value;

        template <is_std_any T>
        void save_to_archive(const T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (value.has_value())
            {
                auto &type_info = value.type();
                if (type_info == typeid(std::nullptr_t))
                {
                    json["%type"] = Engine::Reflection::GetType("std::nullptr_t")->m_name;
                    json["data"] = nullptr;
                }
                else if (type_info == typeid(bool))
                {
                    json["%type"] = Engine::Reflection::GetType("bool")->m_name;
                    json["data"] = std::any_cast<bool>(value);
                }
                else if (type_info == typeid(char))
                {
                    json["%type"] = Engine::Reflection::GetType("char")->m_name;
                    json["data"] = std::any_cast<char>(value);
                }
                else if (type_info == typeid(signed char))
                {
                    json["%type"] = Engine::Reflection::GetType("signed char")->m_name;
                    json["data"] = std::any_cast<signed char>(value);
                }
                else if (type_info == typeid(unsigned char))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned char")->m_name;
                    json["data"] = std::any_cast<unsigned char>(value);
                }
                else if (type_info == typeid(char8_t))
                {
                    json["%type"] = Engine::Reflection::GetType("char8_t")->m_name;
                    json["data"] = std::any_cast<char8_t>(value);
                }
                else if (type_info == typeid(char16_t))
                {
                    json["%type"] = Engine::Reflection::GetType("char16_t")->m_name;
                    json["data"] = std::any_cast<char16_t>(value);
                }
                else if (type_info == typeid(char32_t))
                {
                    json["%type"] = Engine::Reflection::GetType("char32_t")->m_name;
                    json["data"] = std::any_cast<char32_t>(value);
                }
                else if (type_info == typeid(wchar_t))
                {
                    json["%type"] = Engine::Reflection::GetType("wchar_t")->m_name;
                    json["data"] = std::any_cast<wchar_t>(value);
                }
                else if (type_info == typeid(short))
                {
                    json["%type"] = Engine::Reflection::GetType("short")->m_name;
                    json["data"] = std::any_cast<short>(value);
                }
                else if (type_info == typeid(unsigned short))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned short")->m_name;
                    json["data"] = std::any_cast<unsigned short>(value);
                }
                else if (type_info == typeid(int))
                {
                    json["%type"] = Engine::Reflection::GetType("int")->m_name;
                    json["data"] = std::any_cast<int>(value);
                }
                else if (type_info == typeid(unsigned int))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned int")->m_name;
                    json["data"] = std::any_cast<unsigned int>(value);
                }
                else if (type_info == typeid(long))
                {
                    json["%type"] = Engine::Reflection::GetType("long")->m_name;
                    json["data"] = std::any_cast<long>(value);
                }
                else if (type_info == typeid(unsigned long))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned long")->m_name;
                    json["data"] = std::any_cast<unsigned long>(value);
                }
                else if (type_info == typeid(long long))
                {
                    json["%type"] = Engine::Reflection::GetType("long long")->m_name;
                    json["data"] = std::any_cast<long long>(value);
                }
                else if (type_info == typeid(unsigned long long))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned long long")->m_name;
                    json["data"] = std::any_cast<unsigned long long>(value);
                }
                else if (type_info == typeid(float))
                {
                    json["%type"] = Engine::Reflection::GetType("float")->m_name;
                    json["data"] = std::any_cast<float>(value);
                }
                else if (type_info == typeid(double))
                {
                    json["%type"] = Engine::Reflection::GetType("double")->m_name;
                    json["data"] = std::any_cast<double>(value);
                }
                else if (type_info == typeid(long double))
                {
                    json["%type"] = Engine::Reflection::GetType("long double")->m_name;
                    json["data"] = std::any_cast<long double>(value);
                }
                else if (type_info == typeid(std::string))
                {
                    json["%type"] = Engine::Reflection::GetType("std::string")->m_name;
                    json["data"] = std::any_cast<std::string>(value);
                }
                else
                {
                    throw std::runtime_error("Unsupported type for std::any serialization: " + std::string(type_info.name()));
                }
            }
            else
            {
                json = nullptr;
            }
        }

        template <is_std_any T>
        void load_from_archive(T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (!json.is_null())
            {
                auto type_name = json["%type"].get<std::string>();
                if (type_name == "std::nullptr_t")
                {
                    value = nullptr;
                }
                else if (type_name == "bool")
                {
                    value = json["data"].get<bool>();
                }
                else if (type_name == "char")
                {
                    value = json["data"].get<char>();
                }
                else if (type_name == "signed char")
                {
                    value = json["data"].get<signed char>();
                }
                else if (type_name == "unsigned char")
                {
                    value = json["data"].get<unsigned char>();
                }
                else if (type_name == "char8_t")
                {
                    value = json["data"].get<char8_t>();
                }
                else if (type_name == "char16_t")
                {
                    value = json["data"].get<char16_t>();
                }
                else if (type_name == "char32_t")
                {
                    value = json["data"].get<char32_t>();
                }
                else if (type_name == "wchar_t")
                {
                    value = json["data"].get<wchar_t>();
                }
                else if (type_name == "short")
                {
                    value = json["data"].get<short>();
                }
                else if (type_name == "unsigned short")
                {
                    value = json["data"].get<unsigned short>();
                }
                else if (type_name == "int")
                {
                    value = json["data"].get<int>();
                }
                else if (type_name == "unsigned int")
                {
                    value = json["data"].get<unsigned int>();
                }
                else if (type_name == "long")
                {
                    value = json["data"].get<long>();
                }
                else if (type_name == "unsigned long")
                {
                    value = json["data"].get<unsigned long>();
                }
                else if (type_name == "long long")
                {
                    value = json["data"].get<long long>();
                }
                else if (type_name == "unsigned long long")
                {
                    value = json["data"].get<unsigned long long>();
                }
                else if (type_name == "float")
                {
                    value = json["data"].get<float>();
                }
                else if (type_name == "double")
                {
                    value = json["data"].get<double>();
                }
                else if (type_name == "long double")
                {
                    value = json["data"].get<long double>();
                }
                else if (type_name == "std::string")
                {
                    value = json["data"].get<std::string>();
                }
                else
                {
                    throw std::runtime_error("Unsupported type for std::any deserialization: " + type_name);
                }
            }
            else
            {
                value.reset();
            }
        }
    }
}

#endif // REFLECTION_SERIALIZATION_ANY_INCLUDED
