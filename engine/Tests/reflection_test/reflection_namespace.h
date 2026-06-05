#ifndef CTEST_REFLECTION_NAMESPACE_H
#define CTEST_REFLECTION_NAMESPACE_H

#include <Reflection/macros.h>
#include <iostream>

inline const void *NamespaceTest_PrintInfo_Called = nullptr;
inline const void *TestHelloWorld_NamespaceTest_PrintInfo_Called = nullptr;
inline const void *TestHelloWorld_TestHelloWorld2_NamespaceTest_PrintInfo_Called = nullptr;

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

inline void NamespaceTest::PrintInfo() const {
    NamespaceTest_PrintInfo_Called = this;
    std::cout << "NamespaceTest: Hahahaha!" << std::endl;
}

inline void TestHelloWorld::NamespaceTest::PrintInfo() const {
    TestHelloWorld_NamespaceTest_PrintInfo_Called = this;
    std::cout << "TestHelloWorld::NamespaceTest: Hahahaha!" << std::endl;
}

inline void TestHelloWorld::TestHelloWorld2::NamespaceTest::PrintInfo() const {
    TestHelloWorld_TestHelloWorld2_NamespaceTest_PrintInfo_Called = this;
    std::cout << "TestHelloWorld::TestHelloWorld2::NamespaceTest: Hahahaha!" << std::endl;
}

void RunReflectionNamespaceTest();

#endif // CTEST_REFLECTION_NAMESPACE_H
