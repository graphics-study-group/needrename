#ifndef REFLECTION_REFLECTION_INCLUDED
#define REFLECTION_REFLECTION_INCLUDED

#include <vector>
#include <memory>

#include "Type.h"
#include "Field.h"

// Suppress warning from attributes
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

#define REFLECTION [[clang::annotate("reflection")]]

namespace Engine
{
    namespace Reflection
    {
        template<typename T>
        static std::shared_ptr<Type> GetType(const T &obj);
    }
}

#endif // REFLECTION_REFLECTION_INCLUDED
