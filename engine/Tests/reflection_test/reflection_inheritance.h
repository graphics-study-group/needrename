#ifndef CTEST_REFLECTION_INHERITANCE_H
#define CTEST_REFLECTION_INHERITANCE_H

#include <Reflection/macros.h>

class REFL_SER_CLASS(REFL_BLACKLIST) FooBase {
    REFL_SER_BODY(FooBase)
public:
    FooBase() = default;
    virtual ~FooBase() = default;
};

class REFL_SER_CLASS(REFL_BLACKLIST) BBase {
    REFL_SER_BODY(BBase)
public:
    BBase() = default;
    virtual ~BBase() = default;
};

class REFL_SER_CLASS(REFL_WHITELIST) FooA : public FooBase, public BBase {
    REFL_SER_BODY(FooA)
public:
    REFL_ENABLE FooA() = default;
    virtual ~FooA() = default;
};

class REFL_SER_CLASS(REFL_BLACKLIST) VirtualBase {
    REFL_SER_BODY(VirtualBase)
public:
    VirtualBase() = default;
    virtual ~VirtualBase() = default;

    int m_virtual_base = 1;
    virtual void VirtualMethod() {
    }
};

class REFL_SER_CLASS(REFL_WHITELIST) VirtualDerived1 : virtual public VirtualBase {
    REFL_SER_BODY(VirtualDerived1)
public:
    REFL_ENABLE VirtualDerived1() = default;
    virtual ~VirtualDerived1() = default;

    int m_derived1 = 2;
};

class REFL_SER_CLASS(REFL_WHITELIST) VirtualDerived2 : virtual public VirtualBase {
    REFL_SER_BODY(VirtualDerived2)
public:
    REFL_ENABLE VirtualDerived2() = default;
    virtual ~VirtualDerived2() = default;

    int m_derived2 = 3;
};

class REFL_SER_CLASS(REFL_WHITELIST) VirtualDiamond : public VirtualDerived1, public VirtualDerived2 {
    REFL_SER_BODY(VirtualDiamond)
public:
    REFL_ENABLE VirtualDiamond() = default;
    virtual ~VirtualDiamond() = default;

    int m_diamond = 4;
};

class REFL_SER_CLASS(REFL_WHITELIST) NonVirtualBase {
    REFL_SER_BODY(NonVirtualBase)
public:
    REFL_ENABLE NonVirtualBase() = default;
    virtual ~NonVirtualBase() = default;

    int m_nonvirtual_base = 5;
};

class REFL_SER_CLASS(REFL_WHITELIST) NonVirtualDerived : public NonVirtualBase {
    REFL_SER_BODY(NonVirtualDerived)
public:
    REFL_ENABLE NonVirtualDerived() = default;
    virtual ~NonVirtualDerived() = default;

    int m_nonvirtual_derived = 6;
};

void RunReflectionInheritanceTest();

#endif // CTEST_REFLECTION_INHERITANCE_H
