#include <iostream>
#include "reflection_registeration_test.h"

void FooBase::PrintHelloWorld()
{
    std::cout << "Hello World from FooBase!" << std::endl;
}

void BBase::PrintB()
{
    std::cout << "PrintB from BBase!" << std::endl;
}

void FooA::PrintInfo()
{
    std::cout << "FooA: Hahahaha! " << "m_a: " << m_a << " m_b: " << m_b << std::endl;
}

int FooA::Add(int a, int b)
{
    return a + b + m_a + m_b;
}

int FooA::Add(int a, int b, int c)
{
    return a + b + c + m_a + m_b;
}

void FooA::PrintHelloWorld()
{
    std::cout << "Hello World from FooA!" << std::endl;
}

int main()
{
    Engine::Reflection::Initialize();
    auto FooAtype = Engine::Reflection::GetType("FooA");

    std::cout << "(FooA) Var foo:" << std::endl;
    Engine::Reflection::Var foo = FooAtype->CreateInstance(123, 456);
    std::cout << "foo: PrintInfo" << std::endl;
    foo.InvokeMethod("PrintInfo");
    int sum = foo.InvokeMethod("Add", 1, 2).Get<int>();
    std::cout << "Sum 2 args: " << sum << std::endl;
    std::cout << "Sum 3 args: " << foo.InvokeMethod("Add", 1, 2, 3).Get<int>() << std::endl;
    std::cout << "Type of foo: " << foo.m_type->GetName() << std::endl;
    std::cout << "foo.m_a == " << foo.GetMember("m_a").Get<int>() << std::endl;
    std::cout << "Set foo.m_a = 100" << std::endl;
    foo.GetMember("m_a").Set(100);
    std::cout << "foo.m_a == " << foo.GetMember("m_a").Get<int>() << std::endl;
    std::cout << "Base class function: PrintB" << std::endl;
    foo.InvokeMethod("PrintB");

    std::cout << "(FooBase) Var foo_base:" << std::endl;
    Engine::Reflection::Var foo_base = Engine::Reflection::GetType("FooBase")->CreateInstance();
    std::cout << "FooBase: PrintHelloWorld" << std::endl;
    foo_base.InvokeMethod("PrintHelloWorld");

    std::cout << "(Foobase) Var foo2: (it is a FooA in fact)" << std::endl;
    Engine::Reflection::Var foo2(Engine::Reflection::GetType("FooBase"), FooAtype->CreateInstance(5, 7).GetDataPtr());
    std::cout << "foo2: PrintHelloWorld (virtual)" << std::endl;
    foo2.InvokeMethod("PrintHelloWorld");

    return 0;
}
