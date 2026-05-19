#ifndef CTEST_REFLECTION_STDINT_H
#define CTEST_REFLECTION_STDINT_H

#include <Reflection/macros.h>
#include <cstdint>

class REFL_SER_CLASS(REFL_BLACKLIST) Test_stdint {
    REFL_SER_BODY(Test_stdint)
public:
    Test_stdint() = default;
    virtual ~Test_stdint() = default;

    int8_t m_int8 = 0;
    int16_t m_int16 = 0;
    int32_t m_int32 = 0;
    int64_t m_int64 = 0;
    uint8_t m_uint8 = 0;
    uint16_t m_uint16 = 0;
    uint32_t m_uint32 = 0;
    uint64_t m_uint64 = 0;
};

void RunReflectionStdintTest();

#endif // CTEST_REFLECTION_STDINT_H
