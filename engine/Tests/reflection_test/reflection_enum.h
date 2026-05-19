#ifndef CTEST_REFLECTION_ENUM_H
#define CTEST_REFLECTION_ENUM_H

#include <Reflection/macros.h>
#include <cstdint>

class REFL_SER_CLASS(REFL_WHITELIST) EnumClassTest {
    REFL_SER_BODY(EnumClassTest)
public:
    enum class REFL_SER_CLASS() Color {
        Red,
        Green,
        Blue
    };

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

    REFL_ENABLE EnumClassTest() = default;
    virtual ~EnumClassTest() = default;

    REFL_SER_ENABLE Color m_color = Color::Red;
    REFL_SER_ENABLE SmallU8 m_small = SmallU8::A;
    REFL_SER_ENABLE BigU64 m_big = BigU64::Big;
};

void RunReflectionEnumTest();

#endif // CTEST_REFLECTION_ENUM_H
