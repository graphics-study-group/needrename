#ifndef CTEST_SERIALIZATION_ENUM_H
#define CTEST_SERIALIZATION_ENUM_H

#include <Reflection/macros.h>
#include <cstdint>

namespace SerializationTest {
    class REFL_SER_CLASS(REFL_WHITELIST) EnumTest {
        REFL_SER_BODY(EnumTest)
    public:
        EnumTest() = default;
        virtual ~EnumTest() = default;

        REFL_SER_ENABLE enum class TestNormalEnum {
            NE1,
            NE2,
            NE3
        } m_normal_enum = TestNormalEnum::NE1;

        enum class REFL_SER_CLASS() Color {
            Red,
            Green,
            Blue
        };
        REFL_SER_ENABLE Color m_color = Color::Red;

        enum class REFL_SER_CLASS() SmallU8 : uint8_t {
            A = 0,
            B = 1,
            C = 200
        };
        enum class REFL_SER_CLASS() BigU64 : uint64_t {
            Zero = 0ull,
            One = 1ull,
            Big = 0x100000000ull
        };
        enum class BigNoRefl : uint64_t {
            Zero = 0ull,
            One = 1ull,
            Big = 0x100000000ull
        };
        REFL_SER_ENABLE SmallU8 m_small = SmallU8::B;
        REFL_SER_ENABLE BigU64 m_big = BigU64::Big;
        REFL_SER_ENABLE BigNoRefl m_bignorefl = BigNoRefl::Big;
    };
} // namespace SerializationTest

void RunSerializationEnumTest();

#endif // CTEST_SERIALIZATION_ENUM_H
