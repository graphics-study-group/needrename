#include <iostream>
#include "reflection_registeration_test.h"

void FooBase::PrintHelloWorld() const
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

void FooA::PrintHelloWorld() const
{
    std::cout << "Hello World from FooA!" << std::endl;
}

const TestData *ConstTest::GetConstDataPtr() const
{
    return m_const_data;
}

void ConstTest::SetConstDataPtr(const TestData *data)
{
    m_const_data = data;
}

void ConstTest::SetConstDataRef(const TestData &data)
{
    m_const_data = &data;
}

// const TestData &ConstTest::GetConstDataRef() const
// {
//     return *m_const_data;
// }

const TestData *ConstTest::GetTestDataPtrAndAdd()
{
    m_data->data[0] += 1.0f;
    return m_const_data;
}

TestData *ConstTest::GetTestDataPtr()
{
    return m_data;
}

TestData &ConstTest::GetTestDataRef()
{
    return *m_data;
}

void NamespaceTest::PrintInfo() const
{
    std::cout << "NamespaceTest: Hahahaha!" << std::endl;
}

void TestHalloWorld::NamespaceTest::PrintInfo() const
{
    std::cout << "TestHalloWorld::NamespaceTest: Hahahaha!" << std::endl;
}

void TestHalloWorld::TestHalloWorld2::NamespaceTest::PrintInfo() const
{
    std::cout << "TestHalloWorld::TestHalloWorld2::NamespaceTest: Hahahaha!" << std::endl;
}

int main()
{
    Engine::Reflection::Initialize();
    auto FooAtype = Engine::Reflection::GetType("FooA");

    std::cout << "----------------------------------- Test a FooA var -----------------------------------" << std::endl;
    std::cout << "[] (FooA) Var foo:" << std::endl;
    Engine::Reflection::Var foo = FooAtype->CreateInstance(123, 456);
    std::cout << "[] foo: PrintInfo" << std::endl;
    foo.InvokeMethod("PrintInfo");
    int sum = foo.InvokeMethod("Add", 1, 2).Get<int>();
    std::cout << "Sum 2 args: " << sum << std::endl;
    std::cout << "Sum 3 args: " << foo.InvokeMethod("Add", 1, 2, 3).Get<int>() << std::endl;
    std::cout << "Type of foo: " << foo.m_type->GetName() << std::endl;
    std::cout << "foo.m_a == " << foo.GetMember("m_a").Get<int>() << std::endl;
    std::cout << "[] Set foo.m_a = 100" << std::endl;
    foo.GetMember("m_a").Set(100);
    std::cout << "foo.m_a == " << foo.GetMember("m_a").Get<int>() << std::endl;
    std::cout << "[] Base class function: PrintB" << std::endl;
    foo.InvokeMethod("PrintB");

    std::cout << "----------------------------------- Test a FooBase var -----------------------------------" << std::endl;
    std::cout << "[] (FooBase) Var foo_base:" << std::endl;
    Engine::Reflection::Var foo_base = Engine::Reflection::GetType("FooBase")->CreateInstance();
    std::cout << "[] FooBase: PrintHelloWorld" << std::endl;
    foo_base.InvokeMethod("PrintHelloWorld");

    std::cout << "----------------------------------- Test a FooA as a FooBase* var -----------------------------------" << std::endl;
    std::cout << "[] (Foobase) Var foo2: (it is a FooA in fact)" << std::endl;
    Engine::Reflection::Var foo2(Engine::Reflection::GetType("FooBase"), FooAtype->CreateInstance(5, 7).GetDataPtr());
    std::cout << "[] foo2: PrintHelloWorld (virtual)" << std::endl;
    foo2.InvokeMethod("PrintHelloWorld");

    std::cout << "----------------------------------- Test some const, ref, pointer -----------------------------------" << std::endl;
    TestData data1, data2, data3;
    data1.data[0] = 1.0f;
    data2.data[0] = 100.0f;
    data3.data[0] = 1000.0f;
    Engine::Reflection::Var crp_test = Engine::Reflection::GetType("ConstTest")->CreateInstance();
    crp_test.InvokeMethod("SetConstDataPtr", (const TestData *)&data1);
    crp_test.GetMember("m_data").Set(&data2);
    std::cout << "crp_test: m_const_data[0] == " << crp_test.GetMember("m_const_data").Get<const TestData *>()->data[0] << std::endl;
    std::cout << "crp_test: m_data[0] == " << crp_test.GetMember("m_data").Get<TestData *>()->data[0] << std::endl;
    std::cout << "[] crp_test: GetTestDataPtrAndAdd" << std::endl;
    Engine::Reflection::Var data_get = crp_test.InvokeMethod("GetTestDataPtrAndAdd");
    std::cout << "crp_test: m_data[0] == " << crp_test.GetMember("m_data").Get<TestData *>()->data[0] << std::endl;
    std::cout << "data_get: m_data[0] == " << data_get.Get<TestData *>()->data[0] << std::endl;
    std::cout << "[] crp_test: SetTestDataPtr(data3)" << std::endl;
    crp_test.InvokeMethod<const TestData &>("SetConstDataRef", data3); // Attention: Must set the template argument explicitly when passing a reference
    std::cout << "crp_test: m_const_data[0] == " << crp_test.GetMember("m_const_data").Get<const TestData *>()->data[0] << std::endl;
    std::cout << "[] Const Var (const ConstTest) const_ref_var:" << std::endl;
    const ConstTest &const_ref = crp_test.Get<ConstTest>();
    Engine::Reflection::ConstVar const_ref_var = Engine::Reflection::GetConstVar(const_ref);
    Engine::Reflection::ConstVar const_data_get = const_ref_var.InvokeMethod("GetConstDataPtr");
    std::cout << "const_data_get: m_data[0] == " << const_data_get.Get<const TestData *>()->data[0] << std::endl;
    std::cout << "[] make data3 +1.0f" << std::endl;
    data3.data[0] += 1.0f;
    std::cout << "const_data_get: m_data[0] == " << const_data_get.Get<const TestData *>()->data[0] << std::endl;

    std::cout << "----------------------------------- Test namespace of classes -----------------------------------" << std::endl;
    Engine::Reflection::Var ns_test = Engine::Reflection::GetType("NamespaceTest")->CreateInstance();
    ns_test.InvokeMethod("PrintInfo");
    Engine::Reflection::Var ns_test2 = Engine::Reflection::GetType("TestHalloWorld::NamespaceTest")->CreateInstance();
    ns_test2.InvokeMethod("PrintInfo");
    Engine::Reflection::Var ns_test3 = Engine::Reflection::GetType("TestHalloWorld::TestHalloWorld2::NamespaceTest")->CreateInstance();
    ns_test3.InvokeMethod("PrintInfo");

    return 0;
}
