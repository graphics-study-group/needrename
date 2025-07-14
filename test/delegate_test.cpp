#include <iostream>
#include <functional>
#include <memory>
#include <cassert>
#include <Core/Delegate/Delegate.h>
#include <Core/Delegate/Event.h>

int checked = 0;
bool inherit_called = false;

class TestClass
{
public:
    TestClass() = default;
    virtual ~TestClass() = default;

    virtual void PrintInfo(int a, float b)
    {
        std::cout << "TestClass::PrintInfo called with a: " << a << ", b: " << b << std::endl;
        checked++;
    }
};

class Dog
{
public:
    void Bark(int a, float b)
    {
        std::cout << "Dog::Bark called with a: " << a << ", b: " << b << std::endl;
        checked++;
    }
};

class Cat
{
public:
    void Meow(int a, float b)
    {
        std::cout << "Cat::Meow called with a: " << a << ", b: " << b << std::endl;
        checked++;
    }
};

class InheritTest : public TestClass
{
public:
    InheritTest() = default;
    virtual ~InheritTest() = default;

    virtual void PrintInfo(int a, float b) override
    {
        std::cout << "InheritTest::PrintInfo called with a: " << a << ", b: " << b << std::endl;
        inherit_called = true;
    }
};

int main()
{
    using namespace Engine;

    auto testObject = std::make_shared<TestClass>();
    Delegate<int, float> delegate1(testObject, (Delegate<int, float>::FunctionType)std::bind(&TestClass::PrintInfo, testObject.get(), std::placeholders::_1, std::placeholders::_2));
    delegate1.Invoke(42, 3.14f);
    assert(checked == 1); // Check if PrintInfo was called
    testObject.reset();
    delegate1.Invoke(12, 4.2f); // This should not call PrintInfo since testObject is reset
    assert(checked == 1); // Ensure PrintInfo was not called again
    assert(delegate1.IsValid() == false); // Check if delegate is invalid after object reset

    auto testObject2 = std::make_shared<TestClass>();
    Delegate<int, float> delegate2(testObject2, TestClass::PrintInfo);
    Delegate<int, float> delegate3(std::weak_ptr<TestClass>(testObject2), TestClass::PrintInfo);
    delegate2.Invoke(100, 1.23f);
    assert(checked == 2); // Check if PrintInfo was called again
    delegate3.Invoke(200, 4.56f);
    assert(checked == 3); // Check if PrintInfo was called again
    testObject2.reset();
    delegate2.Invoke(300, 7.89f); // This should not call PrintInfo since testObject2 is reset
    assert(checked == 3); // Ensure PrintInfo was not called again
    assert(delegate2.IsValid() == false); // Check if delegate2 is invalid after object reset
    assert(delegate3.IsValid() == false); // Check if delegate3 is invalid after object reset

    Event<int, float> multicastDelegate;
    auto dog = std::make_shared<Dog>();
    multicastDelegate.AddListener(dog, &Dog::Bark);
    auto cat = std::make_shared<Cat>();
    auto handle = multicastDelegate.AddDelegate(std::make_unique<Delegate<int, float>>(cat, &Cat::Meow));
    multicastDelegate.Invoke(1, 2.0f); // Should call both Dog::Bark and Cat::Meow
    assert(checked == 5); // Check if both methods were called
    dog.reset();
    multicastDelegate.Invoke(2, 3.0f); // Should call only Cat::Meow since dog is reset
    assert(checked == 6); // Check if only Cat::Meow was called
    multicastDelegate.ClearInvalidDelegates(); // Should remove invalid delegates
    assert(multicastDelegate.GetDelegateCount() == 1); // Should only have Cat delegate left
    multicastDelegate.RemoveDelegate(handle); // Remove Cat delegate
    assert(multicastDelegate.GetDelegateCount() == 0); // Should have no delegates left

    std::shared_ptr<TestClass> inheritTest = std::make_shared<InheritTest>();
    Delegate<int, float> inheritDelegate(inheritTest, &TestClass::PrintInfo);
    inheritDelegate.Invoke(10, 20.0f); // Should call InheritTest::PrintInfo
    assert(inherit_called == true); // Check if InheritTest::PrintInfo was called
    inheritTest.reset();
    inherit_called = false; // Reset the flag
    inheritDelegate.Invoke(30, 40.0f); // Should not call anything since
    assert(inherit_called == false);
    assert(inheritDelegate.IsValid() == false); // Check if delegate is invalid after object reset

    return 0;
}
