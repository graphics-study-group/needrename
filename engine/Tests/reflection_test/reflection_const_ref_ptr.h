#ifndef CTEST_REFLECTION_CONST_REF_PTR_H
#define CTEST_REFLECTION_CONST_REF_PTR_H

#include <Reflection/macros.h>

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

    class REFL_SER_CLASS(REFL_WHITELIST) ConstTest {
        REFL_SER_BODY(ConstTest)
    public:
        REFL_ENABLE ConstTest() = default;
        virtual ~ConstTest() = default;

        REFL_ENABLE const TestData *m_const_data = nullptr;
        REFL_ENABLE TestData *m_data = nullptr;

        REFL_ENABLE const TestData *GetConstDataPtr() const;
        REFL_ENABLE void SetConstDataPtr(const TestData *data);
        REFL_ENABLE void SetConstDataRef(const TestData &data);
        REFL_ENABLE const TestData &GetConstDataRef() const;
        REFL_ENABLE const TestData *GetTestDataPtrAndAdd();
        REFL_ENABLE TestData *GetTestDataPtr();
        REFL_ENABLE TDR GetTestDataRef();
    };

    inline float TestData::GetData(int idx) const {
        return data[idx];
    }

    inline void TestData::SetData(int idx, float value) {
        data[idx] = value;
    }

    inline const TestData *ConstTest::GetConstDataPtr() const {
        return m_const_data;
    }

    inline void ConstTest::SetConstDataPtr(const TestData *data) {
        m_const_data = data;
    }

    inline void ConstTest::SetConstDataRef(const TestData &data) {
        m_const_data = &data;
    }

    inline const TestData &ConstTest::GetConstDataRef() const {
        return *m_const_data;
    }

    inline const TestData *ConstTest::GetTestDataPtrAndAdd() {
        m_data->data[0] += 1.0f;
        return m_const_data;
    }

    inline TestData *ConstTest::GetTestDataPtr() {
        return m_data;
    }

    inline TDR ConstTest::GetTestDataRef() {
        return *m_data;
    }
} // namespace TestDataNamespace

void RunReflectionConstRefPtrTest();

#endif // CTEST_REFLECTION_CONST_REF_PTR_H
