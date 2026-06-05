#ifndef CTEST_REFLECTION_SMART_POINTER_H
#define CTEST_REFLECTION_SMART_POINTER_H

#include <Reflection/macros.h>
#include <memory>

class REFL_SER_CLASS(REFL_WHITELIST) SmartPointerTest {
    REFL_SER_BODY(SmartPointerTest)
public:
    REFL_ENABLE SmartPointerTest() = default;
    virtual ~SmartPointerTest() = default;

    REFL_ENABLE std::shared_ptr<int> m_shared_ptr = std::make_shared<int>(42);
    REFL_ENABLE std::weak_ptr<int> m_weak_ptr = m_shared_ptr;
    REFL_ENABLE std::unique_ptr<float> m_unique_ptr = std::make_unique<float>(84.0f);
};

void RunReflectionSmartPointerTest();

#endif // CTEST_REFLECTION_SMART_POINTER_H
