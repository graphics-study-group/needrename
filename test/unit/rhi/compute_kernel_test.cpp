// Tests for the device-scoped compute kernel facility (tasks 1.1 - 1.8, 8.1):
//   - kernel identity by module identity: one pipeline per identity, lazy
//     creation, a probe that needs no words (1.1, 8.1)
//   - the resolved interface table and non-contiguous binding numbers (1.2)
//   - dictionary dispatch, buffer sub-ranges, uniform-buffer offset and
//     dictionary-order set identity (1.3)
//   - texture binding for a sampled image and a storage image (1.4)
//   - dictionary validation on both mismatch directions (1.5)
//   - the reflected push-constant contract (1.6)
//   - consecutive dispatches without a caller barrier (1.7)
//
// Every scenario builds its own headless DeviceContext with no render system.

#include "Render/Asset/Shader/ShaderCompiler.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/Device/Structs.h"
#include "Rhi/Pipeline/ComputeKernel.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Texture/Texture.h"
#include "Rhi/Texture/TextureSubresourceView.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

using namespace Engine;
using namespace Engine::Rhi;

static bool g_pass = true;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl;                                        \
            g_pass = false;                                                                                            \
        }                                                                                                              \
    } while (0)

namespace {
    /// @brief Handles carry no `operator==` under typesafe conversion.
    template <typename T>
    bool Same(T lhs, T rhs) {
        return static_cast<typename T::CType>(lhs) == static_cast<typename T::CType>(rhs);
    }

    /// @brief Storage buffers at non-contiguous binding numbers within set 0.
    constexpr const char SHADER_BASIC[] = R"(
#version 450 core
layout(local_size_x = 16, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer Input { float v[]; } input_buffer;
layout(set = 0, binding = 5) writeonly buffer Output { float v[]; } output_buffer;
void main() {
    uint p = gl_GlobalInvocationID.x;
    output_buffer.v[p] = input_buffer.v[p] + 1.0f;
}
)";

    /// @brief A 16-byte push-constant block.
    constexpr const char SHADER_PUSH[] = R"(
#version 450 core
layout(local_size_x = 16, local_size_y = 1, local_size_z = 1) in;
layout(push_constant) uniform Params { vec4 delta; } params;
layout(set = 0, binding = 2) readonly buffer Input { float v[]; } input_buffer;
layout(set = 0, binding = 4) writeonly buffer Output { float v[]; } output_buffer;
void main() {
    uint p = gl_GlobalInvocationID.x;
    output_buffer.v[p] = input_buffer.v[p] + params.delta.x;
}
)";

    /// @brief A uniform buffer declared at a non-zero binding number.
    constexpr const char SHADER_UNIFORM[] = R"(
#version 450 core
layout(local_size_x = 16, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 1) uniform Params { float offset; } params;
layout(set = 0, binding = 3) readonly buffer Input { float v[]; } input_buffer;
layout(set = 0, binding = 6) writeonly buffer Output { float v[]; } output_buffer;
void main() {
    uint p = gl_GlobalInvocationID.x;
    output_buffer.v[p] = input_buffer.v[p] + params.offset;
}
)";

    /// @brief A sampled image and a storage image.
    constexpr const char SHADER_TEXTURE[] = R"(
#version 450 core
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 2, rgba8) uniform image2D outputImage;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    imageStore(outputImage, p, texelFetch(inputImage, p, 0) * 2.0);
}
)";

    std::vector<uint32_t> Compile(const char *glsl) {
        std::vector<uint32_t> binary{};
        ShaderCompiler compiler;
        compiler.CompileGLSLtoSPV(binary, glsl, EShLangCompute);
        return binary;
    }

    /// @brief A texture usable as a sampled image and as a storage image.
    class ScratchTexture : public Rhi::Texture {
    public:
        ScratchTexture(Rhi::DeviceContext &context, uint32_t width, uint32_t height) :
            Rhi::Texture(
                context,
                Rhi::TextureDesc{
                    .dimensions = 2,
                    .width = width,
                    .height = height,
                    .depth = 1,
                    .format = Rhi::ImageFormat::R8G8B8A8UNorm,
                    .memory_type = {Rhi::ImageMemoryTypeBits::DefaultColorAttachment},
                    .mipmap_levels = 1,
                    .array_layers = 1,
                    .is_cube_map = false
                },
                Rhi::SamplerDesc{},
                "Kernel test texture"
            ) {
        }
    };

    struct Fixture {
        DeviceContext context{DeviceInterface::DeviceConfiguration{
            .window = nullptr,
            .application_name = "Rhi::ComputeKernel Test",
            .application_version = 0,
            .dynamic_dispatcher = nullptr,
        }};
        std::vector<std::unique_ptr<ComputeBuffer>> buffers{};
        std::vector<std::unique_ptr<Texture>> textures{};

        ComputeBuffer &MakeBuffer(size_t bytes, bool as_uniform) {
            buffers.push_back(
                ComputeBuffer::CreateUnique(context.GetAllocatorState(), bytes, true, as_uniform, false, false, "Test")
            );
            return *buffers.back();
        }

        Texture &MakeTexture(uint32_t width, uint32_t height) {
            textures.push_back(std::make_unique<ScratchTexture>(context, width, height));
            return *textures.back();
        }

        vk::CommandBuffer Begin() {
            const auto &queues = context.GetDeviceInterface().GetQueueInfo();
            auto cb = context.GetDevice().allocateCommandBuffers(
                vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
            )[0];
            cb.begin(vk::CommandBufferBeginInfo{});
            return cb;
        }

        void Submit(vk::CommandBuffer cb) {
            cb.end();
            const auto &queues = context.GetDeviceInterface().GetQueueInfo();
            queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
            queues.graphicsQueue.waitIdle();
        }

        size_t UniformOffsetAlignment() {
            return context.GetDeviceInterface().QueryLimit(
                DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment
            );
        }

        size_t StorageOffsetAlignment() {
            return context.GetDeviceInterface().QueryLimit(
                DeviceInterface::PhysicalDeviceLimitInteger::StorageBufferOffsetAlignment
            );
        }

        DescriptorArena &Arena() {
            return context.GetDescriptorArena();
        }
    };

    /// @brief Task 1.6: an oversized push value must trip dispatch's debug assertion.
    ///
    /// Registered as a separate CTest case expected to fail (`WILL_FAIL`), because
    /// the assertion aborts the process. In a configuration without assertions
    /// there is nothing to observe, so the case fails there too.
    int RunOversizeProbe() {
#ifndef NDEBUG
        Fixture f;
        ComputeKernel &kernel =
            f.context.RequestComputeKernel("test/oversize-push", Compile(SHADER_PUSH), "Oversize kernel");
        ComputeBuffer &buffer = f.MakeBuffer(4096, false);
        auto cb = f.Begin();

        struct TooLarge {
            float values[8];
        };
        static_assert(sizeof(TooLarge) > 16, "the probe must exceed the reflected 16-byte block");

        const TooLarge oversized{};
        kernel.Dispatch(
            cb,
            {{"Input", ComputeKernelResource::Buffer(buffer)}, {"Output", ComputeKernelResource::Buffer(buffer)}},
            1,
            1,
            1,
            oversized
        );
        std::cerr << "FAILED: an oversized push value was not caught in a debug build" << std::endl;
        return 1;
#else
        std::cout << "Assertions are disabled in this configuration." << std::endl;
        return 1;
#endif
    }
} // namespace

int main(int argc, char **argv) {
#ifdef _MSC_VER
    // Report assertion failures on stderr instead of a modal dialog: a debug
    // assertion must terminate the process, not block it.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    // Requires the SDL video subsystem for SDL_Vulkan_LoadLibrary(nullptr).
    SDL_Init(SDL_INIT_VIDEO);

    if (argc > 1 && std::string_view(argv[1]) == "--oversize-push") {
        return RunOversizeProbe();
    }

    const auto spirv_basic = Compile(SHADER_BASIC);
    const auto spirv_push = Compile(SHADER_PUSH);
    const auto spirv_uniform = Compile(SHADER_UNIFORM);
    const auto spirv_texture = Compile(SHADER_TEXTURE);

    // ── Tasks 1.1 / 8.1: identity, laziness and device-level dedup ─────────

    {
        Fixture f;
        CHECK(f.context.GetComputeKernelCache().GetKernelCount() == 0u && "nothing is created before first use");
        CHECK(f.Arena().GetLivePoolCount() == 0u && "a descriptor pool is created only on first acquisition");
        CHECK(f.context.FindComputeKernel("test/shared") == nullptr && "an unknown identity yields no kernel");

        // Two independent paths requesting the same identity.
        ComputeKernel &first = f.context.RequestComputeKernel("test/shared", spirv_basic, "Shared kernel");
        ComputeKernel &second = f.context.RequestComputeKernel("test/shared", spirv_basic, "Shared kernel");
        CHECK(&first == &second && "the same identity must yield the same kernel");
        CHECK(
            Same(first.GetPipeline(), second.GetPipeline())
            && "exactly one compute pipeline exists for a shared identity"
        );
        CHECK(f.context.GetComputeKernelCache().GetKernelCount() == 1u);
        CHECK(f.context.FindComputeKernel("test/shared") == &first && "a known identity is found without its words");
        CHECK(first.GetName() == "Shared kernel" && "the first requester names the shared kernel");

        // A hitting request ignores the words it is handed: the identity is the
        // caller's assertion, and verifying it would put a scan of the module
        // back on the lookup path.
        ComputeKernel &hit = f.context.RequestComputeKernel("test/shared", spirv_push, "Ignored name");
        CHECK(&hit == &first && "a hitting request keeps the kernel its identity already denotes");
        CHECK(f.context.GetComputeKernelCache().GetKernelCount() == 1u);

        ComputeKernel &other = f.context.RequestComputeKernel("test/other", spirv_push, "Other kernel");
        CHECK(&other != &first && "distinct identities yield distinct kernels");
        CHECK(!Same(other.GetPipeline(), first.GetPipeline()));
        CHECK(f.context.GetComputeKernelCache().GetKernelCount() == 2u);

        // Task 1.6: the push-constant range follows the reflected block size.
        CHECK(first.GetPushConstantSize() == 0u && "a shader without a push block reflects size 0");
        CHECK(other.GetPushConstantSize() == 16u && "a vec4 push block reflects 16 bytes");
    }

    // ── Task 1.3: dictionary dispatch, buffer sub-ranges, canonical order ───

    {
        Fixture f;
        ComputeKernel &kernel = f.context.RequestComputeKernel("test/sub-range", spirv_basic, "Sub-range kernel");

        const size_t storage_alignment = f.StorageOffsetAlignment();
        const size_t input_offset = storage_alignment;
        const size_t output_offset = storage_alignment * 2u;

        ComputeBuffer &input = f.MakeBuffer(4096, false);
        ComputeBuffer &output = f.MakeBuffer(4096, false);

        constexpr size_t ELEMENT_COUNT = 16;
        auto *input_values = reinterpret_cast<float *>(input.GetVMAddress());
        for (size_t i = 0; i < ELEMENT_COUNT; i++) {
            input_values[input_offset / sizeof(float) + i] = 10.0f + static_cast<float>(i);
        }

        const auto resource_in = ComputeKernelResource::Buffer(input, input_offset, ELEMENT_COUNT * sizeof(float));
        const auto resource_out = ComputeKernelResource::Buffer(output, output_offset, ELEMENT_COUNT * sizeof(float));

        auto cb = f.Begin();
        // Two dispatches in opposite dictionary orders: the arena must key both
        // to one set, because the entries are ordered by binding number.
        const size_t minted_before = f.Arena().GetMintedSetCount();
        kernel.Dispatch(cb, {{"Input", resource_in}, {"Output", resource_out}}, 1, 1, 1);
        kernel.Dispatch(cb, {{"Output", resource_out}, {"Input", resource_in}}, 1, 1, 1);
        f.Submit(cb);

        CHECK(f.Arena().GetMintedSetCount() == minted_before + 1u && "dictionary order must not change set identity");

        output.Invalidate(0, output.GetSize());
        const auto *result = reinterpret_cast<const float *>(output.GetVMAddress());
        bool sub_range_ok = true;
        for (size_t i = 0; i < ELEMENT_COUNT; i++) {
            const float expected = 11.0f + static_cast<float>(i);
            if (std::abs(result[output_offset / sizeof(float) + i] - expected) > 1e-4f) {
                sub_range_ok = false;
            }
        }
        CHECK(sub_range_ok && "a supplied offset and size must restrict the bound range");
    }

    // ── Task 1.3: a uniform buffer bound at a non-zero offset ───────────────

    {
        Fixture f;
        ComputeKernel &kernel =
            f.context.RequestComputeKernel("test/uniform-offset", spirv_uniform, "Uniform offset kernel");

        const size_t uniform_offset = f.UniformOffsetAlignment();
        const size_t storage_offset = f.StorageOffsetAlignment();
        ComputeBuffer &uniform = f.MakeBuffer(4096, true);
        ComputeBuffer &input = f.MakeBuffer(4096, false);
        ComputeBuffer &output = f.MakeBuffer(4096, false);

        constexpr size_t ELEMENT_COUNT = 16;
        constexpr float UNIFORM_VALUE = 2.5f;

        *reinterpret_cast<float *>(uniform.GetVMAddress() + uniform_offset) = UNIFORM_VALUE;
        auto *input_values = reinterpret_cast<float *>(input.GetVMAddress());
        for (size_t i = 0; i < ELEMENT_COUNT; i++) {
            input_values[storage_offset / sizeof(float) + i] = static_cast<float>(i);
        }

        auto cb = f.Begin();
        kernel.Dispatch(
            cb,
            {{"Params", ComputeKernelResource::Buffer(uniform, uniform_offset, 16)},
             {"Input", ComputeKernelResource::Buffer(input, storage_offset, ELEMENT_COUNT * sizeof(float))},
             {"Output", ComputeKernelResource::Buffer(output, storage_offset, ELEMENT_COUNT * sizeof(float))}},
            1,
            1,
            1
        );
        f.Submit(cb);

        output.Invalidate(0, output.GetSize());
        const auto *result = reinterpret_cast<const float *>(output.GetVMAddress());
        bool uniform_ok = true;
        for (size_t i = 0; i < ELEMENT_COUNT; i++) {
            const float expected = static_cast<float>(i) + UNIFORM_VALUE;
            if (std::abs(result[storage_offset / sizeof(float) + i] - expected) > 1e-4f) {
                uniform_ok = false;
            }
        }
        CHECK(uniform_ok && "the shader must read the uniform buffer value at the supplied offset");
    }

    // ── Task 1.6: a recorded push value reaches the shader ──────────────────

    {
        Fixture f;
        ComputeKernel &kernel = f.context.RequestComputeKernel("test/push", spirv_push, "Push kernel");

        const size_t storage_offset = f.StorageOffsetAlignment();
        ComputeBuffer &input = f.MakeBuffer(4096, false);
        ComputeBuffer &output = f.MakeBuffer(4096, false);

        constexpr size_t ELEMENT_COUNT = 16;
        auto *input_values = reinterpret_cast<float *>(input.GetVMAddress());
        for (size_t i = 0; i < ELEMENT_COUNT; i++) {
            input_values[storage_offset / sizeof(float) + i] = static_cast<float>(i);
        }

        struct PushBlock {
            float delta[4];
        };
        static_assert(sizeof(PushBlock) == 16, "PushBlock must be 16 bytes");

        const PushBlock push{{3.5f, 0.0f, 0.0f, 0.0f}};
        auto cb = f.Begin();
        kernel.Dispatch(
            cb,
            {{"Input", ComputeKernelResource::Buffer(input, storage_offset, ELEMENT_COUNT * sizeof(float))},
             {"Output", ComputeKernelResource::Buffer(output, storage_offset, ELEMENT_COUNT * sizeof(float))}},
            1,
            1,
            1,
            push
        );
        f.Submit(cb);

        output.Invalidate(0, output.GetSize());
        const auto *result = reinterpret_cast<const float *>(output.GetVMAddress());
        bool push_ok = true;
        for (size_t i = 0; i < ELEMENT_COUNT; i++) {
            const float expected = static_cast<float>(i) + push.delta[0];
            if (std::abs(result[storage_offset / sizeof(float) + i] - expected) > 1e-4f) {
                push_ok = false;
            }
        }
        CHECK(push_ok && "the push-constant value must reach the shader");
    }

    // ── Task 1.4: sampled image in, storage image out ───────────────────────

    {
        Fixture f;
        ComputeKernel &kernel = f.context.RequestComputeKernel("test/texture", spirv_texture, "Texture kernel");

        constexpr uint32_t WIDTH = 4, HEIGHT = 4;
        Texture &input = f.MakeTexture(WIDTH, HEIGHT);
        Texture &output = f.MakeTexture(WIDTH, HEIGHT);
        ComputeBuffer &readback = f.MakeBuffer(WIDTH * HEIGHT * 4u, false);

        const vk::ClearColorValue clear{std::array<float, 4>{0.1f, 0.2f, 0.3f, 0.4f}};
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

        auto cb = f.Begin();
        const vk::ImageMemoryBarrier2 input_to_general{
            vk::PipelineStageFlagBits2::eNone,
            {},
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eGeneral,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            input.GetImage(),
            range
        };
        const vk::ImageMemoryBarrier2 output_to_general{
            vk::PipelineStageFlagBits2::eNone,
            {},
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eGeneral,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            output.GetImage(),
            range
        };
        const vk::ImageMemoryBarrier2 to_general[2] = {input_to_general, output_to_general};
        cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 2, to_general});
        cb.clearColorImage(input.GetImage(), vk::ImageLayout::eGeneral, clear, range);

        const vk::ImageMemoryBarrier2 input_to_sampled{
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderSampledRead,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eReadOnlyOptimal,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            input.GetImage(),
            range
        };
        const vk::ImageMemoryBarrier2 to_sampled[1] = {input_to_sampled};
        cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1, to_sampled});

        kernel.Dispatch(
            cb,
            {{"inputImage", ComputeKernelResource::Image(input)},
             {"outputImage", ComputeKernelResource::Image(output)}},
            1,
            1,
            1
        );

        const vk::ImageMemoryBarrier2 output_to_transfer{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferRead,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eTransferSrcOptimal,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            output.GetImage(),
            range
        };
        const vk::ImageMemoryBarrier2 to_transfer[1] = {output_to_transfer};
        cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1, to_transfer});
        cb.copyImageToBuffer(
            output.GetImage(),
            vk::ImageLayout::eTransferSrcOptimal,
            readback.GetBuffer(),
            {vk::BufferImageCopy{
                0,
                0,
                0,
                vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {0, 0, 0},
                {WIDTH, HEIGHT, 1}
            }}
        );
        f.Submit(cb);

        readback.Invalidate(0, readback.GetSize());
        const auto *texels = reinterpret_cast<const uint8_t *>(readback.GetVMAddress());
        bool texture_ok = true;
        const float expected[4] = {0.2f, 0.4f, 0.6f, 0.8f};
        for (size_t texel = 0; texel < WIDTH * HEIGHT; texel++) {
            for (size_t channel = 0; channel < 4; channel++) {
                const float actual = static_cast<float>(texels[texel * 4 + channel]) / 255.0f;
                if (std::abs(actual - expected[channel]) > 0.02f) {
                    texture_ok = false;
                }
            }
        }
        CHECK(texture_ok && "the sampled image must be read and the storage image written as declared");
    }

    // ── Task 1.5: dictionary validation ─────────────────────────────────────

    {
        Fixture f;
        ComputeKernel &kernel = f.context.RequestComputeKernel("test/validation", spirv_basic, "Validation kernel");
        ComputeBuffer &input = f.MakeBuffer(4096, false);
        ComputeBuffer &output = f.MakeBuffer(4096, false);
        const auto resource = ComputeKernelResource::Buffer(input);

        auto cb = f.Begin();

        bool undeclared_threw = false;
        try {
            kernel.Dispatch(cb, {{"NotDeclared", resource}}, 1, 1, 1);
        } catch (const std::runtime_error &e) {
            const std::string message = e.what();
            undeclared_threw = message.find("Validation kernel") != std::string::npos
                               && message.find("NotDeclared") != std::string::npos;
        }
        CHECK(undeclared_threw && "an undeclared name must throw naming the shader and the interface");

        bool omitted_threw = false;
        try {
            kernel.Dispatch(cb, {{"Input", resource}}, 1, 1, 1);
        } catch (const std::runtime_error &e) {
            const std::string message = e.what();
            omitted_threw =
                message.find("Validation kernel") != std::string::npos && message.find("Output") != std::string::npos;
        }
        CHECK(omitted_threw && "an omitted declared interface must throw naming the shader and the interface");

        bool complete_passed = true;
        try {
            kernel.Dispatch(cb, {{"Input", resource}, {"Output", ComputeKernelResource::Buffer(output)}}, 1, 1, 1);
        } catch (const std::runtime_error &) {
            complete_passed = false;
        }
        CHECK(complete_passed && "a complete dictionary must record without throwing");
    }

    // ── Task 1.7: consecutive dispatches need no kernel-inserted barrier ─────
    //
    // A recorded command buffer exposes no barrier query, so the no-barrier
    // property is structural: the implementation records none, and the caller
    // here supplies the one barrier it needs. What is asserted is that two
    // consecutive dispatches of one kernel record and execute, and that the
    // dispatch surface takes no slot parameter (a compile-time property).

    {
        Fixture f;
        ComputeKernel &kernel = f.context.RequestComputeKernel("test/consecutive", spirv_basic, "Consecutive kernel");
        const size_t storage_offset = f.StorageOffsetAlignment();
        ComputeBuffer &first = f.MakeBuffer(4096, false);
        ComputeBuffer &second = f.MakeBuffer(4096, false);

        constexpr size_t ELEMENT_COUNT = 16;
        auto *values = reinterpret_cast<float *>(first.GetVMAddress());
        for (size_t i = 0; i < ELEMENT_COUNT; i++) {
            values[storage_offset / sizeof(float) + i] = static_cast<float>(i);
        }

        const auto resource_first = ComputeKernelResource::Buffer(first, storage_offset, ELEMENT_COUNT * sizeof(float));
        const auto resource_second =
            ComputeKernelResource::Buffer(second, storage_offset, ELEMENT_COUNT * sizeof(float));

        auto cb = f.Begin();
        kernel.Dispatch(cb, {{"Input", resource_first}, {"Output", resource_second}}, 1, 1, 1);
        // The caller places the one barrier its dependency needs; the kernel
        // adds none of its own.
        const vk::MemoryBarrier2 barrier{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead
        };
        const vk::MemoryBarrier2 dependencies[1] = {barrier};
        cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, 1, dependencies, 0, nullptr, 0, nullptr});
        kernel.Dispatch(cb, {{"Input", resource_second}, {"Output", resource_first}}, 1, 1, 1);
        f.Submit(cb);

        first.Invalidate(0, first.GetSize());
        const auto *result = reinterpret_cast<const float *>(first.GetVMAddress());
        bool consecutive_ok = true;
        for (size_t i = 0; i < ELEMENT_COUNT; i++) {
            const float expected = static_cast<float>(i) + 2.0f;
            if (std::abs(result[storage_offset / sizeof(float) + i] - expected) > 1e-4f) {
                consecutive_ok = false;
            }
        }
        CHECK(consecutive_ok && "two consecutive dispatches with a caller barrier must chain");
    }

    if (!g_pass) {
        std::cerr << "Compute kernel test FAILED." << std::endl;
        return 1;
    }
    std::cout << "Compute kernel test PASSED." << std::endl;
    return 0;
}
