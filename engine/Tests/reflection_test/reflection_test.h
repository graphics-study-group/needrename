#ifndef CTEST_REFLECTION_TEST_H
#define CTEST_REFLECTION_TEST_H

#include <Reflection/macros.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

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

class REFL_SER_CLASS(REFL_BLACKLIST) FooBase {
    REFL_SER_BODY(FooBase)
public:
    FooBase() = default;
    virtual ~FooBase() = default;

    int m_foobase = 1000;

    virtual void PrintHelloWorld() const;
};

class REFL_SER_CLASS(REFL_BLACKLIST) BBase {
    REFL_SER_BODY(BBase)
public:
    BBase() = default;
    virtual ~BBase() = default;

    int m_bbase = 10000;

    void PrintB();

protected:
    int m_protected = 100000;
};

class REFL_SER_CLASS(REFL_WHITELIST) FooA : public FooBase, public BBase {
    REFL_SER_BODY(FooA)
public:
    REFL_ENABLE FooA(int a, int b);
    virtual ~FooA() = default;

    REFL_SER_ENABLE int m_a = 200000;
    REFL_SER_ENABLE int m_b = 300000;

    REFL_ENABLE void PrintInfo();
    REFL_ENABLE int Add(int a, int b);
    REFL_ENABLE int Add(int a, int b, int c);
    REFL_ENABLE virtual void PrintHelloWorld() const;
};

namespace TestDataNamespace {

    class REFL_SER_CLASS(REFL_WHITELIST) TestData {
        REFL_SER_BODY(TestData)
    public:
        REFL_ENABLE TestData() = default;
        virtual ~TestData() = default;

        REFL_SER_ENABLE float data[100] = {0.0f};

        REFL_ENABLE float GetData(int idx) const;
        REFL_ENABLE void SetData(int idx, float value);
    };

    typedef TestData &TDR;
} // namespace TestDataNamespace

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"

namespace TestDataNamespace {
    class REFL_SER_CLASS(REFL_WHITELIST) ConstTest {
        REFL_SER_BODY(ConstTest)
    public:
        REFL_ENABLE ConstTest() = default;
        virtual ~ConstTest() = default;

        REFL_ENABLE const TestData *m_const_data = nullptr;
        REFL_ENABLE TestData *m_data = nullptr;
        // REFL_ENABLE TestData * const m_data_const = nullptr;
        // REFL_ENABLE const int m_const_int = 182376;

        REFL_ENABLE const TestData *GetConstDataPtr() const;
        REFL_ENABLE void SetConstDataPtr(const TestData *data);
        REFL_ENABLE void SetConstDataRef(const TestData &data);
        REFL_ENABLE const TestData &GetConstDataRef() const;
        REFL_ENABLE const TestData *GetTestDataPtrAndAdd();
        REFL_ENABLE TestData *GetTestDataPtr();
        REFL_ENABLE TDR GetTestDataRef();
    };
} // namespace TestDataNamespace

#pragma GCC diagnostic pop

class REFL_SER_CLASS(REFL_WHITELIST) NamespaceTest {
    REFL_SER_BODY(NamespaceTest)
public:
    REFL_ENABLE NamespaceTest() = default;
    virtual ~NamespaceTest() = default;

    REFL_ENABLE void PrintInfo() const;
};

namespace TestHelloWorld {
    class REFL_SER_CLASS(REFL_WHITELIST) NamespaceTest {
        REFL_SER_BODY(NamespaceTest)
    public:
        REFL_ENABLE NamespaceTest() = default;
        virtual ~NamespaceTest() = default;

        REFL_ENABLE void PrintInfo() const;
    };

    namespace TestHelloWorld2 {
        class REFL_SER_CLASS(REFL_WHITELIST) NamespaceTest {
            REFL_SER_BODY(NamespaceTest)
        public:
            REFL_ENABLE NamespaceTest() = default;
            virtual ~NamespaceTest() = default;

            REFL_ENABLE void PrintInfo() const;
        };
    } // namespace TestHelloWorld2
} // namespace TestHelloWorld

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

class REFL_SER_CLASS(REFL_WHITELIST) SmartPointerTest {
    REFL_SER_BODY(SmartPointerTest)
public:
    REFL_ENABLE SmartPointerTest() = default;
    virtual ~SmartPointerTest() = default;

    REFL_ENABLE std::shared_ptr<int> m_shared_ptr = std::make_shared<int>(42);
    REFL_ENABLE std::weak_ptr<int> m_weak_ptr = m_shared_ptr;
    REFL_ENABLE std::unique_ptr<float> m_unique_ptr = std::make_unique<float>(84.0f);
};

class REFL_SER_CLASS(REFL_WHITELIST) EnumClassTest {
    REFL_SER_BODY(EnumClassTest)
public:
    enum class REFL_SER_CLASS() Color {
        Red,
        Green,
        Blue
    };

    // Reflective enums with explicit underlying types for coverage
    enum class REFL_SER_CLASS() SmallU8 : uint8_t {
        A = 0,
        B = 1,
        C = 200
    };

    enum class REFL_SER_CLASS() BigU64 : uint64_t {
        Zero = 0ull,
        One = 1ull,
        Big = 0x100000000ull // > 32-bit to exercise wide underlying type
    };

    REFL_ENABLE EnumClassTest() = default;
    virtual ~EnumClassTest() = default;

    REFL_SER_ENABLE Color m_color = Color::Red;
    REFL_SER_ENABLE SmallU8 m_small = SmallU8::A;
    REFL_SER_ENABLE BigU64 m_big = BigU64::Big;
};

// A reflectable type used to verify that Var construction & destruction
// correctly invoke the underlying object's ctor/dtor. It also embeds a
// smart pointer to ensure owned resources are released when Var is destroyed.
class REFL_SER_CLASS(REFL_WHITELIST) LifecycleTest {
    REFL_SER_BODY(LifecycleTest)
public:
    struct InnerProbe {
        static inline int alive = 0;
        static inline int destroyed = 0;
        InnerProbe() { ++alive; }
        ~InnerProbe() { ++destroyed; --alive; }
    };

    // Default ctor/dtor with counters
    REFL_ENABLE LifecycleTest() : m_probe(std::make_shared<InnerProbe>()) { ++constructed; ++alive; }
    virtual ~LifecycleTest() { ++destructed; --alive; }

    // Return-by-value method to exercise reflected return Var lifetime
    REFL_ENABLE LifecycleTest MakeAnother() const { return LifecycleTest(); }

    // A held smart pointer resource that should be released on destruction
    std::shared_ptr<InnerProbe> m_probe{};

    // Static counters (simple, observable contract for tests)
    static inline int constructed = 0;
    static inline int destructed = 0;
    static inline int alive = 0;

    // Helper for tests (not required to be reflectable)
    static void ResetCounters() {
        constructed = 0;
        destructed = 0;
        alive = 0;
        InnerProbe::destroyed = 0;
        InnerProbe::alive = 0;
    }
};

#endif // CTEST_REFLECTION_TEST_H
