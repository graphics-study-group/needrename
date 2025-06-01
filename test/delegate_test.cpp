#include <iostream>
#include <functional>
#include <memory>
#include <cassert>
#include <Core/Delegate/Delegate.h>

int checked = 0;

class TestClass
{
public:
    void PrintInfo(int a, float b)
    {
        std::cout << "TestClass::PrintInfo called with a: " << a << ", b: " << b << std::endl;
        checked++;
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

    return 0;
}
