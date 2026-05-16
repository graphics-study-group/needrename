#ifndef CTEST_REFLECTION_ARRAY_H
#define CTEST_REFLECTION_ARRAY_H

#include <Reflection/macros.h>
#include <array>
#include <vector>

class REFL_SER_CLASS(REFL_WHITELIST) ArrayTest {
    REFL_SER_BODY(ArrayTest)
public:
    REFL_ENABLE ArrayTest() = default;
    virtual ~ArrayTest() = default;

    REFL_SER_ENABLE int m_array_int[5] = {0};
    REFL_SER_ENABLE int m_array_int2[9][10] = {0};
    REFL_ENABLE std::vector<float> m_vector_float{};
    REFL_ENABLE std::array<double, 12> m_array_double{};
};

void RunReflectionArrayTest();

#endif // CTEST_REFLECTION_ARRAY_H
