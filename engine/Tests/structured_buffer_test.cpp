#include "../Render/Memory/StructuredBuffer.h"
#include "../Render/Memory/StructuredBufferPlacer.h"
#include <glm.hpp>
#include <iostream>
#include <format>
#include <gtc/type_ptr.hpp>

struct [[gnu::packed]] sub_buffer {
    uint32_t v1;
    double v2;
    float v3[3];
};

struct [[gnu::packed]] super_buffer{
    uint64_t v1;
    sub_buffer v2;
    float v3[16];
};

int main() {
    using namespace Engine;
    StructuredBufferPlacer sub_placer, super_placer;

    sub_placer.AddVariable<uint32_t>("v1", offsetof(sub_buffer, v1));
    sub_placer.AddVariable<double>("v2", offsetof(sub_buffer, v2));
    sub_placer.AddVariable<float[3]>("v3", offsetof(sub_buffer, v3));

    super_placer.AddVariable<uint64_t>("v1", offsetof(super_buffer, v1));
    super_placer.AddStructuredBuffer("v2", offsetof(super_buffer, v2), sub_placer);
    super_placer.AddVariable<float[16]>("v3", offsetof(super_buffer, v3));

    std::cout << std::format("sub placer size {}, expected {}", sub_placer.CalculateMaxSize(), sizeof(sub_buffer)) << std::endl;
    std::cout << std::format("super placer size {}, expected {}", super_placer.CalculateMaxSize(), sizeof(super_buffer)) << std::endl;

    assert(sub_placer.CalculateMaxSize() >= sizeof(sub_buffer));
    assert(super_placer.CalculateMaxSize() >= sizeof(super_buffer));

    std::vector <std::byte> buffer;
    buffer.resize(1024);

    StructuredBuffer subsb, supersb;
    glm::vec3 v{1.0f, 2.0f, 3.0f};
    glm::mat4 m{1.0f};

    subsb.SetVariable<uint32_t>("v1", 0x55AA55AA);
    subsb.SetVariable<double>("v2", 1e9);
    subsb.SetVariable<float[3]>("v3", glm::value_ptr(v));

    supersb.SetVariable<uint64_t>("v1", 0xAA55AA55);
    supersb.SetStructuredBuffer("v2", subsb);
    supersb.SetVariable<float[16]>("v3", glm::value_ptr(m));

    {
        sub_placer.WriteBuffer(subsb, buffer);

        sub_buffer sb;
        std::memcpy(&sb, buffer.data(), sizeof(sb));
        assert(sb.v1 == 0x55AA55AA);
        assert(sb.v2 == 1e9);
        assert(sb.v3[0] == 1.0f && sb.v3[1] == 2.0f && sb.v3[2] == 3.0f);
    }
    
    {
        super_placer.WriteBuffer(supersb, buffer);
        super_buffer supb;
        std::memcpy(&supb, buffer.data(), sizeof(supb));
        assert(supb.v1 == 0xAA55AA55);
        assert(supb.v2.v1 == 0x55AA55AA);
        assert(supb.v2.v2 == 1e9);
        assert(supb.v2.v3[0] == 1.0f && supb.v2.v3[1] == 2.0f && supb.v2.v3[2] == 3.0f);
    }
}
