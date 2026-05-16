#ifndef CTEST_REFLECTION_METHOD_INVOKE_H
#define CTEST_REFLECTION_METHOD_INVOKE_H

#include <Reflection/macros.h>
#include <iostream>

inline const void *FooBase_PrintHelloWorld_Called = nullptr;
inline void *BBase_PrintB_Called = nullptr;
inline void *FooA_FooA_Called = nullptr;
inline void *FooA_PrintInfo_Called = nullptr;
inline void *FooA_Add2_Called = nullptr;
inline void *FooA_Add3_Called = nullptr;
inline const void *FooA_PrintHelloWorld_Called = nullptr;

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

inline void FooBase::PrintHelloWorld() const {
    FooBase_PrintHelloWorld_Called = this;
    std::cout << "Hello World from FooBase!" << std::endl;
}

inline void BBase::PrintB() {
    BBase_PrintB_Called = this;
    std::cout << "PrintB from BBase!" << std::endl;
}

inline FooA::FooA(int a, int b) : m_a(a), m_b(b) {
    FooA_FooA_Called = this;
}

inline void FooA::PrintInfo() {
    FooA_PrintInfo_Called = this;
    std::cout << "FooA: Hahahaha! " << "m_a: " << m_a << " m_b: " << m_b << std::endl;
}

inline int FooA::Add(int a, int b) {
    FooA_Add2_Called = this;
    return a + b + m_a + m_b;
}

inline int FooA::Add(int a, int b, int c) {
    FooA_Add3_Called = this;
    return a + b + c + m_a + m_b;
}

inline void FooA::PrintHelloWorld() const {
    FooA_PrintHelloWorld_Called = this;
    std::cout << "Hello World from FooA!" << std::endl;
}

void RunReflectionMethodInvokeTest();

#endif // CTEST_REFLECTION_METHOD_INVOKE_H
