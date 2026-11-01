#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Texture/ImageTexture.h"

#include <cstdio>
#include <cstdlib>
#include <webgpu/webgpu.h>

/**
 * @brief Task 8.1: the desktop validation target creates a device and reports its limits.
 *
 * The device context is the whole of this test - constructing it creates the instance,
 * requests an adapter and a device, reads the limits block and builds the allocator, the
 * caches and the arena. If any of that fails it throws, and the test fails with the reason.
 *
 * The assertions are about what the device *says*, not about what it does: a limit of zero
 * for any of the four the seam exposes would mean the query did not reach the device, which
 * is the failure this test exists to catch.
 *
 * Built only when the backend under test is WebGPU; the Vulkan suite is where the rest of
 * the engine's tests run.
 */
int main() {
    Engine::Rhi::DeviceInterface::DeviceConfiguration cfg{
        .window = nullptr,
        .application_name = "webgpu_device_test",
        .application_version = 1,
    };

    try {
        Engine::Rhi::DeviceContext context(cfg);
        const Engine::Rhi::DeviceCapabilities &caps = context.GetCapabilities();

        std::printf(
            "webgpu_device_test: push constants %u, uniform alignment %llu, storage range %llu, "
            "compute invocations %u, bind groups %u\n",
            caps.max_push_constant_bytes,
            static_cast<unsigned long long>(caps.uniform_buffer_offset_alignment),
            static_cast<unsigned long long>(caps.max_storage_buffer_range),
            caps.max_compute_invocations,
            caps.max_bind_groups
        );

        // The four the seam asks about must be real numbers. Push constants are zero by
        // design on this backend - a parameter block is a uniform buffer here - so it is
        // excluded rather than asserted non-zero.
        if (caps.uniform_buffer_offset_alignment == 0) {
            std::fprintf(stderr, "webgpu_device_test: uniform alignment was not reported.\n");
            return 1;
        }
        if (caps.max_storage_buffer_range == 0) {
            std::fprintf(stderr, "webgpu_device_test: storage buffer range was not reported.\n");
            return 1;
        }
        if (caps.max_compute_invocations == 0) {
            std::fprintf(stderr, "webgpu_device_test: compute invocations were not reported.\n");
            return 1;
        }
        if (caps.max_bind_groups == 0) {
            std::fprintf(stderr, "webgpu_device_test: bind group limit was not reported.\n");
            return 1;
        }

        // The device is usable for the two resource kinds the render path starts with: a
        // buffer that can be a vertex source and a texture that can be sampled.
        Engine::Rhi::DeviceBuffer::Create(
            context.GetAllocatorState(),
            Engine::Rhi::BufferType{Engine::Rhi::BufferTypeBits::Vertex},
            1024,
            "webgpu_device_test.buffer"
        );

        Engine::Rhi::ImageTexture::ImageTextureDesc desc{};
        desc.dimensions = 2;
        desc.width = 4;
        desc.height = 4;
        desc.depth = 1;
        desc.format = Engine::Rhi::ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm;
        desc.mipmap_levels = 1;
        desc.array_layers = 1;
        desc.is_cube_map = false;
        auto texture = Engine::Rhi::ImageTexture::CreateUnique(
            context, desc, Engine::Rhi::SamplerDesc{}, "webgpu_device_test.texture"
        );
        if (!texture->GetImage().IsValid()) {
            std::fprintf(stderr, "webgpu_device_test: the texture has no image.\n");
            return 1;
        }

        std::printf("webgpu_device_test: device, buffer and texture created.\n");
        return 0;
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "webgpu_device_test: device creation failed: %s\n", ex.what());
        return 1;
    }
}



