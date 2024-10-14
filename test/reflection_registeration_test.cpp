#include <iostream>
#include "Reflection/reflection.h"

class REFLECTION FooA
{
public:
    FooA() = default;
    ~FooA() = default;

    REFLECTION void PrintInfo()
    {
        std::cout << "FooA" << std::endl;
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
        namespace Reflection_FooA
        {
            void wrapperConstructor(void *obj, void *&ret, std::vector<void *> args)
            {
                ret = static_cast<void *>(new FooA());
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
            static void Register_FooA()
            {
                std::shared_ptr<Type> type = std::make_shared<Type>();
                type->setName("FooA");
                type->AddMethod("$Constructor", Reflection_FooA::wrapperConstructor);
                type->AddMethod("PrintInfo", Reflection_FooA::wrapperPrintInfo);
                type->AddMethod("Add", Reflection_FooA::wrapperAdd);
                Type::s_typeMap["FooA"] = type;
            }

            static void RegisterAllTypes()
            {
                Register_FooA();
            }
        };
    }
}

int main()
{
    Engine::Reflection::Registrar::RegisterAllTypes();
    auto type = Engine::Reflection::Type::s_typeMap["FooA"];

    std::shared_ptr<FooA> foo = std::shared_ptr<FooA>(type->CreateInstance<FooA>());
    type->InvokeMethod(foo.get(), "PrintInfo");
    int sum = *static_cast<int *>(type->InvokeMethod(foo.get(), "Add", 1, 2));
    std::cout << "Sum: " << sum << std::endl;

    void *foo2 = type->CreateInstance();
    type->InvokeMethod(foo2, "PrintInfo");
    sum = *static_cast<int *>(type->InvokeMethod(foo2, "Add", 3, 4));
    std::cout << "Sum: " << sum << std::endl;

    return 0;
}
