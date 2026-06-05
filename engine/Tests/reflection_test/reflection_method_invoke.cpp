#include "reflection_method_invoke.h"

#include "meta_reflection_method_invoke/reflection_init.inc"
#include <Reflection/reflection.h>
#include <cassert>
#include <iostream>

namespace {
    void InitializeReflectionRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionMethodInvokeTest() {
    auto foo_a_type = Engine::Reflection::GetType("FooA");
    std::cout << "----------------------------------- Test a FooA var -----------------------------------" << std::endl;
    std::cout << "[] (FooA) Var foo:" << std::endl;
    Engine::Reflection::Var foo = foo_a_type->CreateInstance(123, 456);
    void *foo_data = foo.GetDataPtr();
    assert(foo_data == FooA_FooA_Called);
    std::cout << "[] foo: PrintInfo" << std::endl;
    foo.InvokeMethod("PrintInfo");
    assert(foo_data == FooA_PrintInfo_Called);

    int sum = foo.InvokeMethod("Add", 1, 2).Get<int>();
    std::cout << "Sum 2 args: " << sum << std::endl;
    assert(foo_data == FooA_Add2_Called);
    assert(sum == 123 + 456 + 1 + 2);

    sum = foo.InvokeMethod("Add", 1, 2, 3).Get<int>();
    std::cout << "Sum 3 args: " << sum << std::endl;
    assert(foo_data == FooA_Add3_Called);
    assert(sum == 123 + 456 + 1 + 2 + 3);

    assert(foo.GetType()->GetName() == "FooA");
    assert(foo.GetMember("m_a").Get<int>() == 123);
    foo.GetMember("m_a").Set(100);
    assert(foo.GetMember("m_a").Get<int>() == 100);
    foo.InvokeMethod("PrintB");
    assert(foo_data == BBase_PrintB_Called);

    std::cout << "----------------------------------- Test a FooBase var -----------------------------------"
              << std::endl;
    Engine::Reflection::Var foo_base = Engine::Reflection::GetType("FooBase")->CreateInstance();
    void *foo_base_data = foo_base.GetDataPtr();
    foo_base.InvokeMethod("PrintHelloWorld");
    assert(foo_base_data == FooBase_PrintHelloWorld_Called);

    std::cout << "----------------------------------- Test a FooA as a FooBase* var -----------------------------------"
              << std::endl;
    Engine::Reflection::Var foo2_origin = foo_a_type->CreateInstance(5, 7);
    Engine::Reflection::Var foo2(Engine::Reflection::GetType("FooBase"), foo2_origin.GetDataPtr());
    void *foo2_data = foo2.GetDataPtr();
    foo2.InvokeMethod("PrintHelloWorld");
    assert(foo2_data == FooA_PrintHelloWorld_Called);
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionMethodInvokeTest();
    return 0;
}

#include "__generated__/reflection_method_invoke.h.inc"
