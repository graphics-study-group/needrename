#include "reflection.h"
#include "Type.h"

#include "meta_engine_reflection_init.ipp"

namespace Engine
{
    namespace Reflection
    {
        void Initialize()
        {
            Registrar::RegisterBasicTypes();
            RegisterAllTypes();
        }

        void Registrar::RegisterBasicTypes()
        {
            Type::s_type_map["void"] = Type::s_type_map[typeid(void).name()] = std::shared_ptr<Type>(new Type("void", &typeid(void), false));
            Type::s_type_map["int"] = Type::s_type_map[typeid(int).name()] = std::shared_ptr<Type>(new Type("int", &typeid(int), false));
            Type::s_type_map["float"] = Type::s_type_map[typeid(float).name()] = std::shared_ptr<Type>(new Type("float", &typeid(float), false));
            Type::s_type_map["double"] = Type::s_type_map[typeid(double).name()] = std::shared_ptr<Type>(new Type("double", &typeid(double), false));
            Type::s_type_map["char"] = Type::s_type_map[typeid(char).name()] = std::shared_ptr<Type>(new Type("char", &typeid(char), false));
            Type::s_type_map["bool"] = Type::s_type_map[typeid(bool).name()] = std::shared_ptr<Type>(new Type("bool", &typeid(bool), false));
            Type::s_type_map["short"] = Type::s_type_map[typeid(short).name()] = std::shared_ptr<Type>(new Type("short", &typeid(short), false));
            Type::s_type_map["long"] = Type::s_type_map[typeid(long).name()] = std::shared_ptr<Type>(new Type("long", &typeid(long), false));
            Type::s_type_map["unsigned int"] = Type::s_type_map[typeid(unsigned int).name()] = std::shared_ptr<Type>(new Type("unsigned int", &typeid(unsigned int), false));
            Type::s_type_map["unsigned char"] = Type::s_type_map[typeid(unsigned char).name()] = std::shared_ptr<Type>(new Type("unsigned char", &typeid(unsigned char), false));
            Type::s_type_map["unsigned short"] = Type::s_type_map[typeid(unsigned short).name()] = std::shared_ptr<Type>(new Type("unsigned short", &typeid(unsigned short), false));
            Type::s_type_map["unsigned long"] = Type::s_type_map[typeid(unsigned long).name()] = std::shared_ptr<Type>(new Type("unsigned long", &typeid(unsigned long), false));
            Type::s_type_map["long long"] = Type::s_type_map[typeid(long long).name()] = std::shared_ptr<Type>(new Type("long long", &typeid(long long), false));
            Type::s_type_map["unsigned long long"] = Type::s_type_map[typeid(unsigned long long).name()] = std::shared_ptr<Type>(new Type("unsigned long long", &typeid(unsigned long long), false));
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
