#ifndef CTEST_SERIALIZATION_STDINT_H
#define CTEST_SERIALIZATION_STDINT_H

#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <cstdint>

namespace SerializationTest {
    class REFL_SER_CLASS(REFL_BLACKLIST) StdintTest {
        REFL_SER_BODY(StdintTest)
    public:
        StdintTest() = default;
        virtual ~StdintTest() = default;

        int8_t m_int8 = 0;
        int16_t m_int16 = 0;
        int32_t m_int32 = 0;
        int64_t m_int64 = 0;
        uint8_t m_uint8 = 0;
        uint16_t m_uint16 = 0;
        uint32_t m_uint32 = 0;
        uint64_t m_uint64 = 0;
    };
} // namespace SerializationTest

void RunSerializationStdintTest();

#endif // CTEST_SERIALIZATION_STDINT_H
