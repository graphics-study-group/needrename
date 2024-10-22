#include <iostream>
#include "Reflection/reflection.h"

class REFLECTION FooBase
{
public:
    FooBase() = default;
    virtual ~FooBase() = default;

    REFLECTION int m_foobase = 1324;

    REFLECTION virtual void PrintHelloWorld()
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

    REFLECTION int Add(int a, int b, int c)
    {
        return a + b + c + m_a + m_b;
    }

    REFLECTION virtual void PrintHelloWorld()
    {
        std::cout << "Hello World from FooA!" << std::endl;
    }
};

namespace Engine
{
    namespace Reflection
    {
        class Registrar
        {
        public:
            static std::shared_ptr<Type> Register_FooBase()
            {
                std::shared_ptr<Type> type = std::make_shared<Type>("FooBase", &typeid(FooBase), true);
                type->AddMethod(
                    std::make_shared<Method>(
                        std::string(Type::constructer_name) + GetMangledName<>(),
                        [](void *obj, void *&ret, std::vector<void *> args)
                        { ret = static_cast<void *>(new FooBase()); },
                        type));
                type->AddMethod(
                    "PrintHelloWorld",
                    [](void *obj, void *&ret, std::vector<void *> args)
                    { static_cast<FooBase *>(obj)->PrintHelloWorld(); },
                    GetType("void"),
                    &FooBase::PrintHelloWorld);
                type->AddField(GetType("int"), "m_foobase", &FooBase::m_foobase);
                Type::s_type_map["FooBase"] = type;
                return type;
            }

            static std::shared_ptr<Type> Register_BBase()
            {
                std::shared_ptr<Type> type = std::make_shared<Type>("BBase", &typeid(BBase), true);
                type->AddMethod(
                    std::make_shared<Method>(
                        std::string(Type::constructer_name) + GetMangledName<>(),
                        [](void *obj, void *&ret, std::vector<void *> args)
                        { ret = static_cast<void *>(new BBase()); },
                        type));
                type->AddMethod(
                    "PrintB",
                    [](void *obj, void *&ret, std::vector<void *> args)
                    { static_cast<BBase *>(obj)->PrintB(); },
                    GetType("void"),
                    &BBase::PrintB);
                type->AddField(GetType("int"), "m_bbase", &BBase::m_bbase);
                Type::s_type_map["BBase"] = type;
                return type;
            }

            static std::shared_ptr<Type> Register_FooA()
            {
                std::shared_ptr<Type> type = std::make_shared<Type>("FooA", &typeid(FooA), true);
                type->AddMethod(
                    std::make_shared<Method>(
                        std::string(Type::constructer_name) + GetMangledName<int, int>(),
                        [](void *obj, void *&ret, std::vector<void *> args)
                        { ret = static_cast<void *>(new FooA(*static_cast<int *>(args[0]), *static_cast<int *>(args[1]))); },
                        type));
                type->AddMethod(
                    "PrintInfo",
                    [](void *obj, void *&ret, std::vector<void *> args)
                    { static_cast<FooA *>(obj)->PrintInfo(); },
                    GetType("void"),
                    &FooA::PrintInfo);
                type->AddMethod(
                    "Add",
                    [](void *obj, void *&ret, std::vector<void *> args)
                    { int *ret_ptr = static_cast<int *>(malloc(sizeof(int))); *ret_ptr = static_cast<FooA *>(obj)->Add(*static_cast<int *>(args[0]), *static_cast<int *>(args[1])); ret = static_cast<void *>(ret_ptr); },
                    GetType("int"),
                    (int(FooA::*)(int, int)) & FooA::Add);
                type->AddMethod(
                    "Add",
                    [](void *obj, void *&ret, std::vector<void *> args)
                    { int *ret_ptr = static_cast<int *>(malloc(sizeof(int))); *ret_ptr = static_cast<FooA *>(obj)->Add(*static_cast<int *>(args[0]), *static_cast<int *>(args[1]), *static_cast<int *>(args[2])); ret = static_cast<void *>(ret_ptr); },
                    GetType("int"),
                    (int(FooA::*)(int, int, int)) & FooA::Add);
                type->AddMethod(
                    "PrintHelloWorld",
                    [](void *obj, void *&ret, std::vector<void *> args)
                    { static_cast<FooA *>(obj)->PrintHelloWorld(); },
                    GetType("void"),
                    &FooA::PrintHelloWorld);
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
    std::cout << "Sum3: " << foo.InvokeMethod("Add", 1, 2, 3).Get<int>() << std::endl;
    std::cout << "Type of foo: " << foo.m_type->GetName() << std::endl;
    Engine::Reflection::Var base(Engine::Reflection::GetType("FooBase"), foo.GetDataPtr());
    base.InvokeMethod("PrintHelloWorld");

    Engine::Reflection::Var foo2 = type->CreateInstance(84651, 4532);
    foo2.InvokeMethod("PrintInfo");
    sum = foo2.InvokeMethod("Add", 3, 4).Get<int>();
    std::cout << "Sum: " << sum << std::endl;
    foo2.InvokeMethod("PrintHelloWorld");

    std::cout << foo.GetMember("m_a").Get<int>() << std::endl;

    std::cout << Engine::Reflection::GetMangledName<int, char, float, double>() << std::endl;
    // std::cout << Engine::Reflection::GetFunctionArgsMangledName(FooA::Add) << std::endl;

    return 0;
}
