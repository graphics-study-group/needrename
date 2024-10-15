#include <iostream>
#include "Reflection/reflection.h"

class REFLECTION FooBase
{
public:
    FooBase() = default;
    virtual ~FooBase() = default;

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

    REFLECTION void PrintB()
    {
        std::cout << "B" << std::endl;
    }
};

class REFLECTION FooA : public FooBase, public BBase
{
    int m_a;
    int m_b;

public:
    FooA(int a, int b) : m_a(a), m_b(b) {}
    virtual ~FooA() = default;

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
                std::shared_ptr<Type> type = std::make_shared<Type>();
                type->setName("FooBase");
                type->AddMethod(Type::constructer_name, Reflection_FooBase::wrapperConstructor);
                type->AddMethod("PrintHelloWorld", Reflection_FooBase::wrapperPrintHelloWorld);
                Type::s_type_map["FooBase"] = type;
                return type;
            }

            static std::shared_ptr<Type> Register_BBase()
            {
                std::shared_ptr<Type> type = std::make_shared<Type>();
                type->setName("BBase");
                type->AddMethod(Type::constructer_name, Reflection_BBase::wrapperConstructor);
                type->AddMethod("PrintB", Reflection_BBase::wrapperPrintB);
                Type::s_type_map["BBase"] = type;
                return type;
            }

            static std::shared_ptr<Type> Register_FooA()
            {
                std::shared_ptr<Type> type = std::make_shared<Type>();
                type->setName("FooA");
                type->AddMethod(Type::constructer_name, Reflection_FooA::wrapperConstructor);
                type->AddMethod("PrintInfo", Reflection_FooA::wrapperPrintInfo);
                type->AddMethod("Add", Reflection_FooA::wrapperAdd);
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
        std::shared_ptr<Type> GetType(const T &obj)
        {
            if constexpr (std::is_same_v<T, FooA>)
                return Type::s_type_map["FooA"];
            else if constexpr (std::is_same_v<T, FooBase>)
                return Type::s_type_map["FooBase"];
            else if constexpr (std::is_same_v<T, BBase>)
                return Type::s_type_map["BBase"];
            else
                return nullptr;
        }
    }
}

int main()
{
    Engine::Reflection::Registrar::RegisterAllTypes();
    auto type = Engine::Reflection::Type::s_type_map["FooA"];

    std::shared_ptr<FooA> foo = std::shared_ptr<FooA>(static_cast<FooA *>(type->CreateInstance(123, 456)));
    type->InvokeMethod(foo.get(), "PrintInfo");
    int sum = *static_cast<int *>(type->InvokeMethod(foo.get(), "Add", 1, 2));
    std::cout << "Sum: " << sum << std::endl;
    std::cout << "Type of foo: " << Engine::Reflection::GetType(*foo)->GetName() << std::endl;

    void *foo2 = type->CreateInstance(84651, 4532);
    type->InvokeMethod(foo2, "PrintInfo");
    sum = *static_cast<int *>(type->InvokeMethod(foo2, "Add", 3, 4));
    std::cout << "Sum: " << sum << std::endl;
    type->InvokeMethod(foo2, "PrintHelloWorld");

    return 0;
}
