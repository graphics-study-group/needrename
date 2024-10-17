#include <iostream>
#include "Reflection/reflection.h"

class REFLECTION FooBase
{
public:
    FooBase() = default;
    virtual ~FooBase() = default;

    REFLECTION int m_foobase = 1324;

    REFLECTION void PrintHelloWorld()
    {
        std::cout << "Hello World!" << std::endl;
    }
};

class REFLECTION BBase
{
public:
    BBase() = default;
    virtual ~BBase() = default;

    REFLECTION int m_bbase = 23;

    REFLECTION void PrintB()
    {
        std::cout << "B" << std::endl;
    }
};

class REFLECTION FooA : public FooBase, public BBase
{
public:
    FooA(int a, int b) : m_a(a), m_b(b) {}
    virtual ~FooA() = default;

    REFLECTION int m_a = 42;
    REFLECTION int m_b = 800;

    REFLECTION void PrintInfo()
    {
        std::cout << "FooA " << m_a + m_b << std::endl;
    }

    REFLECTION int Add(int a, int b)
    {
        return a + b;
    }
};

namespace Engine
{
    namespace Reflection
    {
        namespace Reflection_FooBase
        {
            void wrapperConstructor(void *obj, void *&ret, std::vector<void *> args)
            {
                ret = static_cast<void *>(new FooBase());
            }

            void wrapperPrintHelloWorld(void *obj, void *&ret, std::vector<void *> args)
            {
                static_cast<FooBase *>(obj)->PrintHelloWorld();
            }
        }

        namespace Reflection_BBase
        {
            void wrapperConstructor(void *obj, void *&ret, std::vector<void *> args)
            {
                ret = static_cast<void *>(new BBase());
            }

            void wrapperPrintB(void *obj, void *&ret, std::vector<void *> args)
            {
                static_cast<BBase *>(obj)->PrintB();
            }
        }

        namespace Reflection_FooA
        {
            void wrapperConstructor(void *obj, void *&ret, std::vector<void *> args)
            {
                ret = static_cast<void *>(new FooA(*static_cast<int *>(args[0]), *static_cast<int *>(args[1])));
            }

            void wrapperPrintInfo(void *obj, void *&ret, std::vector<void *> args)
            {
                static_cast<FooA *>(obj)->PrintInfo();
            }

            void wrapperAdd(void *obj, void *&ret, std::vector<void *> args)
            {
                int *ret_ptr = static_cast<int *>(malloc(sizeof(int)));
                *ret_ptr = static_cast<FooA *>(obj)->Add(*static_cast<int *>(args[0]), *static_cast<int *>(args[1]));
                ret = static_cast<void *>(ret_ptr);
            }
        }

        class Registrar
        {
        public:
            static std::shared_ptr<Type> Register_FooBase()
            {
                std::shared_ptr<Type> type = std::make_shared<Type>("FooBase", &typeid(FooBase), true);
                type->AddMethod(Type::constructer_name, std::make_shared<Method>(Type::constructer_name, Reflection_FooBase::wrapperConstructor, type));
                type->AddMethod("PrintHelloWorld", std::make_shared<Method>("PrintHelloWorld", Reflection_FooBase::wrapperPrintHelloWorld, GetType("void")));
                type->AddField(GetType("int"), "m_foobase", &FooBase::m_foobase);
                Type::s_type_map["FooBase"] = type;
                return type;
            }

            static std::shared_ptr<Type> Register_BBase()
            {
                std::shared_ptr<Type> type = std::make_shared<Type>("BBase", &typeid(BBase), true);
                type->AddMethod(Type::constructer_name, std::make_shared<Method>(Type::constructer_name, Reflection_BBase::wrapperConstructor, type));
                type->AddMethod("PrintB", std::make_shared<Method>("PrintB", Reflection_BBase::wrapperPrintB, GetType("void")));
                type->AddField(GetType("int"), "m_bbase", &BBase::m_bbase);
                Type::s_type_map["BBase"] = type;
                return type;
            }

            static std::shared_ptr<Type> Register_FooA()
            {
                std::shared_ptr<Type> type = std::make_shared<Type>("FooA", &typeid(FooA), true);
                type->AddMethod(Type::constructer_name, std::make_shared<Method>(Type::constructer_name, Reflection_FooA::wrapperConstructor, type));
                type->AddMethod("PrintInfo", std::make_shared<Method>("PrintInfo", Reflection_FooA::wrapperPrintInfo, GetType("void")));
                type->AddMethod("Add", std::make_shared<Method>("Add", Reflection_FooA::wrapperAdd, GetType("int")));
                type->AddField(GetType("int"), "m_a", &FooA::m_a);
                type->AddField(GetType("int"), "m_b", &FooA::m_b);
                Type::s_type_map["FooA"] = type;
                return type;
            }

            static void RegisterAllTypes()
            {
                auto Type_FooBase = Register_FooBase();
                auto Type_BBase = Register_BBase();
                auto Type_FooA = Register_FooA();

                Type_FooA->AddBaseType(Type_FooBase);
                Type_FooA->AddBaseType(Type_BBase);
            }
        };

        template <typename T>
        std::shared_ptr<Type> GetTypeFromObject(const T &obj)
        {
            if constexpr (std::is_same_v<T, FooA>)
                return Type::s_type_map["FooA"];
            else if constexpr (std::is_same_v<T, FooBase>)
                return Type::s_type_map["FooBase"];
            else if constexpr (std::is_same_v<T, BBase>)
                return Type::s_type_map["BBase"];
            else
                return GetType(typeid(T).name());
        }
    }
}

int main()
{
    Engine::Reflection::Initialize();
    Engine::Reflection::Registrar::RegisterAllTypes();
    auto type = Engine::Reflection::Type::s_type_map["FooA"];

    Engine::Reflection::Var foo = type->CreateInstance(123, 456);
    foo.InvokeMethod("PrintInfo");
    int sum = foo.InvokeMethod("Add", 1, 2).Get<int>();
    std::cout << "Sum: " << sum << std::endl;
    std::cout << "Type of foo: " << foo.m_type->GetName() << std::endl;

    Engine::Reflection::Var foo2 = type->CreateInstance(84651, 4532);
    foo2.InvokeMethod("PrintInfo");
    sum = foo2.InvokeMethod("Add", 3, 4).Get<int>();
    std::cout << "Sum: " << sum << std::endl;
    foo2.InvokeMethod("PrintHelloWorld");

    std::cout << foo.GetMember("m_a").Get<int>() << std::endl;

    return 0;
}
