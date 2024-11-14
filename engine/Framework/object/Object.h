#ifndef FRMAEWORK_OBJECT_OBJECT_INCLUDED
#define FRMAEWORK_OBJECT_OBJECT_INCLUDED

#include <Core/guid.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    /// @brief Base class for all objects in runtime
    class REFL_SER_CLASS(REFL_WHITELIST) Object
    {
    public:
        REFL_ENABLE Object() = default;
        virtual ~Object() = default;

    protected:
        GUID m_id {};
    };
}

#endif // FRMAEWORK_OBJECT_OBJECT_INCLUDED
