#ifndef CTEST_REFLECTION_NESTED_MEMBER_H
#define CTEST_REFLECTION_NESTED_MEMBER_H

#include <Reflection/macros.h>
#include <string>

struct REFL_SER_CLASS(REFL_WHITELIST) NestedStruct {
    REFL_SER_BODY(NestedStruct)
public:
    REFL_ENABLE NestedStruct() = default;
    virtual ~NestedStruct() = default;

    REFL_SER_ENABLE int level1_value = 42;
    REFL_SER_ENABLE std::string level1_name = "level1";
};

struct REFL_SER_CLASS(REFL_WHITELIST) MiddleStruct {
    REFL_SER_BODY(MiddleStruct)
public:
    REFL_ENABLE MiddleStruct() = default;
    virtual ~MiddleStruct() = default;

    REFL_SER_ENABLE NestedStruct nested_member;
    REFL_SER_ENABLE int middle_value = 100;
    REFL_SER_ENABLE std::string middle_name = "middle";
};

struct REFL_SER_CLASS(REFL_WHITELIST) TopLevelStruct {
    REFL_SER_BODY(TopLevelStruct)
public:
    REFL_ENABLE TopLevelStruct() = default;
    virtual ~TopLevelStruct() = default;

    REFL_SER_ENABLE MiddleStruct middle_member;
    REFL_SER_ENABLE int top_value = 200;
    REFL_SER_ENABLE std::string top_name = "top";
};

void RunReflectionNestedMemberTest();

#endif // CTEST_REFLECTION_NESTED_MEMBER_H
