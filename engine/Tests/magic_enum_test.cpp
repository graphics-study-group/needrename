#include <Render/Enums/enum_def.h>
#include <Render/Enums/enum_from_string.h>
#include <Render/Enums/enum_to_string.h>
#include <cassert>

using namespace Engine::_enums;

#define DEFINE_ENUM_ITEM(e, n) assert(e::n == e ## _from_string(to_string(e::n))); \
assert(#n == to_string(e::n)); \
assert(e::n == e ## _from_string(#n));

int main() {

    #include <Render/Enums/defs/AddressMode.def>
    #include <Render/Enums/defs/BlendFactor.def>
    #include <Render/Enums/defs/BlendOperation.def>
    #include <Render/Enums/defs/Comparator.def>
    #include <Render/Enums/defs/CullingMode.def>
    #include <Render/Enums/defs/FillingMode.def>
    #include <Render/Enums/defs/FilterMode.def>
    #include <Render/Enums/defs/FrontFace.def>
    #include <Render/Enums/defs/ImageFormat.def>
    #include <Render/Enums/defs/StencilOperation.def>

    return 0;
}
