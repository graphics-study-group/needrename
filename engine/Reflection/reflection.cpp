#include "reflection.h"
#include "Type.h"

namespace Engine
{
    namespace Reflection
    {
        void Initialize()
        {
            RegisterBasicTypes();
            // Registrar::RegisterAllTypes();
        }

        void RegisterBasicTypes()
        {
            Type::s_type_map["void"] = Type::s_type_map[typeid(void).name()] = std::make_shared<Type>("void", &typeid(void), false);
            Type::s_type_map["int"] = Type::s_type_map[typeid(int).name()] = std::make_shared<Type>("int", &typeid(int), false);
            Type::s_type_map["float"] = Type::s_type_map[typeid(float).name()] = std::make_shared<Type>("float", &typeid(float), false);
            Type::s_type_map["double"] = Type::s_type_map[typeid(double).name()] = std::make_shared<Type>("double", &typeid(double), false);
            Type::s_type_map["char"] = Type::s_type_map[typeid(char).name()] = std::make_shared<Type>("char", &typeid(char), false);
            Type::s_type_map["bool"] = Type::s_type_map[typeid(bool).name()] = std::make_shared<Type>("bool", &typeid(bool), false);
            Type::s_type_map["short"] = Type::s_type_map[typeid(short).name()] = std::make_shared<Type>("short", &typeid(short), false);
            Type::s_type_map["long"] = Type::s_type_map[typeid(long).name()] = std::make_shared<Type>("long", &typeid(long), false);
            Type::s_type_map["unsigned int"] = Type::s_type_map[typeid(unsigned int).name()] = std::make_shared<Type>("unsigned int", &typeid(unsigned int), false);
            Type::s_type_map["unsigned char"] = Type::s_type_map[typeid(unsigned char).name()] = std::make_shared<Type>("unsigned char", &typeid(unsigned char), false);
            Type::s_type_map["unsigned short"] = Type::s_type_map[typeid(unsigned short).name()] = std::make_shared<Type>("unsigned short", &typeid(unsigned short), false);
            Type::s_type_map["unsigned long"] = Type::s_type_map[typeid(unsigned long).name()] = std::make_shared<Type>("unsigned long", &typeid(unsigned long), false);
            Type::s_type_map["long long"] = Type::s_type_map[typeid(long long).name()] = std::make_shared<Type>("long long", &typeid(long long), false);
            Type::s_type_map["unsigned long long"] = Type::s_type_map[typeid(unsigned long long).name()] = std::make_shared<Type>("unsigned long long", &typeid(unsigned long long), false);
        }

        std::shared_ptr<Type> GetType(const std::string &name)
        {
            if (Type::s_type_map.find(name) != Type::s_type_map.end())
                return Type::s_type_map[name];
            Type::s_type_map[name] = std::make_shared<Type>(name, nullptr, false);
            return Type::s_type_map[name];
        }
    }
}
