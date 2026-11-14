// The WebGPU recording and submission path: compute scopes, constant publication and
// dispatch-time validation (tasks 2.1, 2.2, 3.1, 3.2, 3.3, 4.1, 4.2, 4.3, 5.3).
//
// Every case here drives the real backend through its public seam: a device context, command
// buffers, encoders and compute kernels built from WGSL. What is being checked is the shape of the
// work, not its speed:
//
//   - consecutive dispatches share one compute scope, and a copy or the end of the recording closes
//     it, so a finished command buffer never leaves a pass open;
//   - a command buffer's constant windows reach the device in one queue write made just before its
//     submission, each window zero-filled past the recorded prefix, and each dispatch reads its own;
//   - two command buffers recorded interleaved and submitted in a known order keep their own values
//     even though their device windows are at the same offsets;
//   - a recording that exceeds the ring reports an error instead of wrapping;
//   - the dictionary a dispatch is given is canonicalized (so the arena reuses one bind group
//     regardless of order) and the validation the backend performs - undeclared names, wrong
//     resource kinds, writable aliasing - still throws in this configuration.
//
// It is built only when the backend under test is WebGPU.

#include "Rhi/Backend/WebGPU/Command/RecordingState.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Command/CommandBuffer.h"
#include "Rhi/Command/CommandEncoder.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/Fence.h"
#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/Pipeline/ComputeKernel.h"
#include "Rhi/Pipeline/ShaderInterfaceDescription.h"
#include "Rhi/Pipeline/ShaderModule.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Texture/ImageTexture.h"

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <webgpu/webgpu.h>

using namespace Engine::Rhi;

namespace {
    int g_failures = 0;

    void Check(bool condition, const char *what) {
        if (condition) {
            return;
        }
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++g_failures;
    }

    /// @brief A kernel that copies the whole constant block into a four-word result buffer.
    ///
    /// The block is declared as four words so a caller that pushes fewer than sixteen bytes can be
    /// checked for zero padding: the shader reads the whole declared struct, which is what makes
    /// the padding observable rather than a guess.
    constexpr const char *k_constant_kernel = R"(
struct Params {
    a: u32,
    b: u32,
    c: u32,
    d: u32,
};
@group(3) @binding(0) var<uniform> params: Params;
@group(0) @binding(0) var<storage, read_write> result: array<u32, 4>;

@compute @workgroup_size(1)
fn main() {
    result[0] = params.a;
    result[1] = params.b;
    result[2] = params.c;
    result[3] = params.d;
}
)";

    /// @brief Two writable storage bindings of one module, for the aliasing rules.
    constexpr const char *k_two_writable_kernel = R"(
@group(0) @binding(0) var<storage, read_write> first: array<u32>;
@group(0) @binding(1) var<storage, read_write> second: array<u32>;

@compute @workgroup_size(1)
fn main() {
    first[0] = second[0] + 1u;
}
)";

    /// @brief The same two bindings, both read-only, which WebGPU allows to alias one buffer.
    constexpr const char *k_two_readonly_kernel = R"(
@group(0) @binding(0) var<storage, read> first: array<u32>;
@group(0) @binding(1) var<storage, read> second: array<u32>;

var<workgroup> sink: u32;

@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    // Reads both bindings and writes nothing a caller can observe: this kernel exists so that
    // two read-only bindings of one buffer can be dispatched.
    sink = first[id.x] + second[id.x];
}
)";

    std::span<const std::byte> Bytes(const char *text) {
        return std::as_bytes(std::span{text, std::strlen(text)});
    }

    /// @brief One storage-buffer interface at set 0 binding 0, optionally with a constant block.
    ShaderInterfaceDescription OneStorageBinding(const char *name, bool with_constant_block, uint32_t block_size) {
        ShaderInterfaceDescription description{};
        description.stage = "compute";
        description.entry_point = "main";
        ShaderInterfaceBinding binding{};
        binding.set = 0;
        binding.binding = 0;
        binding.kind = "storage-buffer";
        binding.name = name;
        binding.storage_read_only = false;
        description.bindings.push_back(binding);
        if (with_constant_block) {
            description.has_constant_block = true;
            description.constant_block = ShaderInterfaceDescription::ConstantBlock{3, 0, block_size};
        }
        return description;
    }

    /// @brief Two storage-buffer interfaces in set 0, at bindings zero and one.
    ShaderInterfaceDescription TwoStorageBindings(const char *first, const char *second, bool read_only) {
        ShaderInterfaceDescription description{};
        description.stage = "compute";
        description.entry_point = "main";
        for (uint32_t index = 0; index < 2; index++) {
            ShaderInterfaceBinding binding{};
            binding.set = 0;
            binding.binding = index;
            binding.kind = "storage-buffer";
            binding.name = index == 0 ? first : second;
            binding.storage_read_only = read_only;
            description.bindings.push_back(binding);
        }
        return description;
    }

    /// @brief A storage buffer the shader writes, which can be copied out or written by a copy.
    std::unique_ptr<DeviceBuffer> CreateResultBuffer(AllocatorState &allocator, size_t bytes, const char *name) {
        const BufferType type = BufferType{BufferTypeBits::ShaderWrite} | BufferTypeBits::ShaderReadOnly
                                | BufferTypeBits::CopyFrom | BufferTypeBits::CopyTo;
        return DeviceBuffer::CreateUnique(allocator, type, bytes, name);
    }

    /// @brief A buffer the host maps to read what the shader wrote.
    std::unique_ptr<DeviceBuffer> CreateReadbackBuffer(AllocatorState &allocator, size_t bytes, const char *name) {
        return DeviceBuffer::CreateUnique(allocator, BufferType{BufferTypeBits::ReadbackFromDevice}, bytes, name);
    }

    /// @brief Submit one finished command buffer and block until the device has run it.
    bool SubmitAndWait(DeviceContext &context, FenceHandle fence, CommandBuffer &buffer) {
        Submission submission{};
        submission.command_buffers.push_back(buffer.GetHandle());
        context.Submit(submission, fence);
        return context.WaitForFence(fence, std::numeric_limits<uint64_t>::max());
    }

    /**
     * @brief Copy a device-written buffer into a readback buffer, run it, and read the words.
     *
     * @return False when the copy or the map did not complete; `out` is untouched then.
     */
    bool ReadBack(
        DeviceContext &context,
        FenceHandle fence,
        DeviceBuffer &source,
        DeviceBuffer &readback,
        uint32_t *out,
        size_t word_count
    ) {
        const size_t bytes = word_count * sizeof(uint32_t);
        auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
        CommandBuffer &buffer = buffers[0];
        buffer.Begin();
        {
            CommandEncoder encoder{buffer.GetHandle()};
            encoder.CopyBuffer(source.GetBuffer(), readback.GetBuffer(), bytes);
        }
        buffer.End();
        if (!SubmitAndWait(context, fence, buffer)) {
            std::fprintf(stderr, "FAILED: the read-back copy did not complete\n");
            return false;
        }
        readback.PrepareReadback();
        const auto *words = reinterpret_cast<const uint32_t *>(readback.GetVMAddress());
        if (words == nullptr) {
            std::fprintf(stderr, "FAILED: the read-back buffer has no host mapping\n");
            return false;
        }
        std::memcpy(out, words, bytes);
        // Given back so the next copy in this test may target the same buffer again.
        readback.ReleaseReadback();
        return true;
    }

    /// @brief One dispatch's constant block, as four words.
    std::array<std::byte, 16> ConstantWords(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        std::array<std::byte, 16> bytes{};
        const std::array<uint32_t, 4> words{a, b, c, d};
        std::memcpy(bytes.data(), words.data(), sizeof(words));
        return bytes;
    }
} // namespace

int main() {
    // SDL_Log drops messages until SDL is initialised, and a failure here is reported through it.
    SDL_Init(0);

    DeviceInterface::DeviceConfiguration cfg{
        .window = nullptr,
        .application_name = "webgpu_submission_path_test",
        .application_version = 1,
    };

    try {
        DeviceContext context{cfg};
        AllocatorState &allocator = context.GetAllocatorState();
        const Fence fence = Fence::Create(context.GetDeviceInterface(), false, "submission path test");
        const FenceHandle fence_handle = fence.GetHandle();

        // ── Kernels ───────────────────────────────────────────────────────
        ShaderModule constant_module =
            ShaderModule::Create(context.GetDeviceInterface(), Bytes(k_constant_kernel), "submission_path.constant");
        ComputeKernel &constant_kernel = context.RequestComputeKernelFromModule(
            "submission_path.constant",
            constant_module.GetHandle(),
            OneStorageBinding("Result", true, 16),
            "main",
            "submission_path.constant"
        );
        Check(constant_kernel.GetPushConstantSize() == 16, "the kernel reports its 16-byte constant block");

        constexpr size_t k_result_bytes = 4 * sizeof(uint32_t);
        auto result_a = CreateResultBuffer(allocator, k_result_bytes, "submission_path.result_a");
        auto result_b = CreateResultBuffer(allocator, k_result_bytes, "submission_path.result_b");
        auto result_c = CreateResultBuffer(allocator, k_result_bytes, "submission_path.result_c");
        auto readback = CreateReadbackBuffer(allocator, k_result_bytes, "submission_path.readback");
        Check(
            result_a != nullptr && result_b != nullptr && result_c != nullptr && readback != nullptr,
            "the test's buffers were created"
        );

        std::array<uint32_t, 4> words{};

        // ── 2.1 / 2.2: consecutive dispatches share one compute scope ─────
        //
        // Two dispatches, different constant values and different target buffers, then a copy. The
        // copy closes the scope and the recording finishes with nothing open; both dispatches ran
        // and each read its own value.
        ResetSubmissionCounters();
        {
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            {
                CommandEncoder encoder{buffer.GetHandle()};
                const auto first = ConstantWords(11, 0, 0, 0);
                const auto second = ConstantWords(22, 0, 0, 0);
                constant_kernel.Dispatch(encoder, {{"Result", *result_a}}, 1, 1, 1, first);
                constant_kernel.Dispatch(encoder, {{"Result", *result_b}}, 1, 1, 1, second);
                encoder.CopyBuffer(result_a->GetBuffer(), readback->GetBuffer(), k_result_bytes);
            }
            buffer.End();
            Check(SubmitAndWait(context, fence_handle, buffer), "the shared-scope recording completed");
        }
        {
            const SubmissionCounters &counters = GetSubmissionCounters();
            Check(counters.compute_passes == 1, "two consecutive dispatches opened one compute scope");
            Check(counters.constant_writes == 1, "one command buffer published its constants once");
            Check(counters.constant_windows == 2, "both constant windows were published");
        }
        if (ReadBack(context, fence_handle, *result_a, *readback, words.data(), words.size())) {
            Check(words[0] == 11, "the first dispatch read its own constant value");
        }
        if (ReadBack(context, fence_handle, *result_b, *readback, words.data(), words.size())) {
            Check(words[0] == 22, "the second dispatch read its own constant value");
        }

        // ── 2.2: a copy in the middle closes the scope ────────────────────
        ResetSubmissionCounters();
        {
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            {
                CommandEncoder encoder{buffer.GetHandle()};
                const auto before = ConstantWords(31, 0, 0, 0);
                const auto after = ConstantWords(32, 0, 0, 0);
                constant_kernel.Dispatch(encoder, {{"Result", *result_a}}, 1, 1, 1, before);
                // Not a compute command: the scope ends here and the copy observes the dispatch.
                encoder.CopyBuffer(result_a->GetBuffer(), readback->GetBuffer(), k_result_bytes);
                constant_kernel.Dispatch(encoder, {{"Result", *result_b}}, 1, 1, 1, after);
            }
            buffer.End();
            Check(SubmitAndWait(context, fence_handle, buffer), "the pass-transition recording completed");
        }
        {
            const SubmissionCounters &counters = GetSubmissionCounters();
            Check(
                counters.compute_passes == 2, "the copy closed the first scope and the later dispatch opened another"
            );
            Check(counters.constant_writes == 1, "both windows still left in one queue write");
        }
        if (ReadBack(context, fence_handle, *result_a, *readback, words.data(), words.size())) {
            Check(words[0] == 31, "the dispatch before the copy ran");
        }
        if (ReadBack(context, fence_handle, *result_b, *readback, words.data(), words.size())) {
            Check(words[0] == 32, "the dispatch after the copy ran");
        }

        // ── 4.1: a short block is zero-padded to its declared window ──────
        {
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            {
                CommandEncoder encoder{buffer.GetHandle()};
                // Four bytes of a sixteen-byte block: the shader reads all four words.
                const uint32_t only_first = 0x11223344u;
                constant_kernel.Dispatch(encoder, {{"Result", *result_a}}, 1, 1, 1, only_first);
                encoder.CopyBuffer(result_a->GetBuffer(), readback->GetBuffer(), k_result_bytes);
            }
            buffer.End();
            Check(SubmitAndWait(context, fence_handle, buffer), "the short-constant recording completed");
        }
        if (ReadBack(context, fence_handle, *result_a, *readback, words.data(), words.size())) {
            Check(words[0] == 0x11223344u, "the recorded prefix reached the shader");
            Check(words[1] == 0 && words[2] == 0 && words[3] == 0, "the rest of the window reads as zero");
        }

        // ── 4.2: a submission with no constants writes none ───────────────
        ResetSubmissionCounters();
        {
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            {
                CommandEncoder encoder{buffer.GetHandle()};
                encoder.CopyBuffer(result_a->GetBuffer(), result_c->GetBuffer(), k_result_bytes);
            }
            buffer.End();
            Check(SubmitAndWait(context, fence_handle, buffer), "the constant-free recording completed");
            Check(GetSubmissionCounters().constant_writes == 0, "a submission with no constants performed no write");
            Check(GetSubmissionCounters().queue_submits == 1, "the constant-free submission was one submit");
        }

        // ── 4.1: the ring's capacity is an error, not a wrap ──────────────
        {
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            bool threw = false;
            std::string message{};
            try {
                CommandEncoder encoder{buffer.GetHandle()};
                encoder.BindComputePipeline(constant_kernel.GetPipeline(), constant_kernel.GetPipelineLayout());
                const auto block = ConstantWords(1, 0, 0, 0);
                // The ring holds 4096 windows of 256 bytes; the 4097th does not fit.
                for (size_t index = 0; index < 4097; index++) {
                    encoder.SetKernelParams(std::span<const std::byte>(block).first(4));
                }
            } catch (const std::length_error &error) {
                threw = true;
                message = error.what();
            }
            Check(threw, "recording more windows than the ring holds reported an error");
            Check(message.find("constant ring") != std::string::npos, "the overflow diagnostic names the ring");
            // The command buffer is destroyed without being finished: the destruction path has to
            // close the open scope rather than leave it dangling.
        }

        // ── 4.3: interleaved recording, known submission order ────────────
        //
        // A and B are recorded into alternately, so both use device windows at offset zero. They are
        // submitted A then B; A's value must survive, which is only true if B's bytes reach the
        // device after A's submission.
        ResetSubmissionCounters();
        {
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 2);
            CommandBuffer &buffer_a = buffers[0];
            CommandBuffer &buffer_b = buffers[1];
            buffer_a.Begin();
            buffer_b.Begin();
            {
                CommandEncoder encoder_a{buffer_a.GetHandle()};
                CommandEncoder encoder_b{buffer_b.GetHandle()};
                const auto block_a = ConstantWords(0xAAAA0001u, 0, 0, 0);
                const auto block_b = ConstantWords(0xBBBB0002u, 0, 0, 0);
                // Interleaved on purpose: A records a window, then B does, then the dispatches.
                constant_kernel.Dispatch(encoder_a, {{"Result", *result_a}}, 1, 1, 1, block_a);
                constant_kernel.Dispatch(encoder_b, {{"Result", *result_b}}, 1, 1, 1, block_b);
            }
            buffer_a.End();
            buffer_b.End();

            Submission submission{};
            submission.command_buffers.push_back(buffer_a.GetHandle());
            submission.command_buffers.push_back(buffer_b.GetHandle());
            context.Submit(submission, fence_handle);
            Check(
                context.WaitForFence(fence_handle, std::numeric_limits<uint64_t>::max()),
                "the interleaved submission completed"
            );
            Check(
                GetSubmissionCounters().constant_writes == 2, "each constant-bearing command buffer performed one write"
            );
        }
        if (ReadBack(context, fence_handle, *result_a, *readback, words.data(), words.size())) {
            Check(words[0] == 0xAAAA0001u, "the earlier command buffer kept its own constant value");
        }
        if (ReadBack(context, fence_handle, *result_b, *readback, words.data(), words.size())) {
            Check(words[0] == 0xBBBB0002u, "the later command buffer read its own constant value");
        }

        // ── 3.2: the dictionary's order does not reach the arena ──────────
        ShaderModule pair_module =
            ShaderModule::Create(context.GetDeviceInterface(), Bytes(k_two_writable_kernel), "submission_path.pair");
        ComputeKernel &pair_kernel = context.RequestComputeKernelFromModule(
            "submission_path.pair",
            pair_module.GetHandle(),
            TwoStorageBindings("First", "Second", false),
            "main",
            "submission_path.pair"
        );
        {
            const size_t minted_before = context.GetDescriptorArena().GetMintedSetCount();
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            {
                CommandEncoder encoder{buffer.GetHandle()};
                pair_kernel.Dispatch(encoder, {{"First", *result_a}, {"Second", *result_b}}, 1, 1, 1);
                // The same two resources, the other way round: same content, so the arena must
                // reuse the bind group it minted for the first dispatch.
                pair_kernel.Dispatch(encoder, {{"Second", *result_b}, {"First", *result_a}}, 1, 1, 1);
            }
            buffer.End();
            Check(SubmitAndWait(context, fence_handle, buffer), "the reordered-dictionary recording completed");
            Check(
                context.GetDescriptorArena().GetMintedSetCount() == minted_before + 1,
                "a dictionary in another order reused the same descriptor set"
            );
        }

        // ── 3.1 / 3.3: the validation the backend performs still throws ───
        {
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            CommandEncoder encoder{buffer.GetHandle()};

            bool undeclared = false;
            try {
                constant_kernel.Dispatch(encoder, {{"NotDeclared", *result_a}}, 1, 1, 1, ConstantWords(1, 0, 0, 0));
            } catch (const std::runtime_error &error) {
                undeclared = std::string{error.what()}.find("NotDeclared") != std::string::npos;
            }
            Check(undeclared, "a name the kernel does not declare is rejected and named");

            static_cast<void>(constant_module);
            static_cast<void>(pair_kernel);
        }

        // A wrong resource kind needs a texture, which the kernel declares as a buffer.
        {
            ImageTexture::ImageTextureDesc desc{};
            desc.dimensions = 2;
            desc.width = 4;
            desc.height = 4;
            desc.depth = 1;
            desc.format = ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm;
            desc.mipmap_levels = 1;
            desc.array_layers = 1;
            desc.is_cube_map = false;
            auto texture = ImageTexture::CreateUnique(context, desc, SamplerDesc{}, "submission_path.texture");
            Check(texture != nullptr && texture->GetImage().IsValid(), "the test's texture was created");

            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            CommandEncoder encoder{buffer.GetHandle()};
            bool wrong_kind = false;
            try {
                constant_kernel.Dispatch(
                    encoder, {{"Result", ComputeKernelResource::Image(*texture)}}, 1, 1, 1, ConstantWords(1, 0, 0, 0)
                );
            } catch (const std::runtime_error &error) {
                wrong_kind = std::string{error.what()}.find("a buffer, but an image was supplied") != std::string::npos;
            }
            Check(wrong_kind, "an image supplied for a buffer interface is rejected");
        }

        // Writable aliasing: one buffer through two writable storage bindings.
        {
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            CommandEncoder encoder{buffer.GetHandle()};
            bool aliasing = false;
            try {
                pair_kernel.Dispatch(encoder, {{"First", *result_a}, {"Second", *result_a}}, 1, 1, 1);
            } catch (const std::runtime_error &error) {
                const std::string text{error.what()};
                aliasing = text.find("forbids that aliasing") != std::string::npos
                           && text.find("submission_path.pair") != std::string::npos;
            }
            Check(aliasing, "writable storage aliasing is rejected and names the kernel");
        }

        // Read-only aliasing of the same buffer is what every shader that reads its input twice
        // does, and it must not be rejected.
        ShaderModule readonly_module = ShaderModule::Create(
            context.GetDeviceInterface(), Bytes(k_two_readonly_kernel), "submission_path.readonly"
        );
        ComputeKernel &readonly_kernel = context.RequestComputeKernelFromModule(
            "submission_path.readonly",
            readonly_module.GetHandle(),
            TwoStorageBindings("First", "Second", true),
            "main",
            "submission_path.readonly"
        );
        {
            bool rejected = false;
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            {
                CommandEncoder encoder{buffer.GetHandle()};
                try {
                    readonly_kernel.Dispatch(encoder, {{"First", *result_a}, {"Second", *result_a}}, 1, 1, 1);
                } catch (const std::runtime_error &error) {
                    rejected = true;
                    std::fprintf(stderr, "FAILED: read-only aliasing was rejected: %s\n", error.what());
                }
            }
            buffer.End();
            Check(!rejected, "two read-only bindings of one buffer are accepted");
            Check(SubmitAndWait(context, fence_handle, buffer), "the read-only-aliasing recording completed");
        }

        static_cast<void>(result_c);

        // ── 1.3: the explicit wait does not poll with short sleeps ────────
        //
        // Three shapes of the caller's wait. An idle queue answers immediately without asking the
        // device; submitted work completes under an infinite budget; and a generous finite budget
        // is handed to the platform rather than spent in a sleep loop.
        {
            auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
            CommandBuffer &buffer = buffers[0];
            buffer.Begin();
            {
                CommandEncoder encoder{buffer.GetHandle()};
                encoder.CopyBuffer(result_a->GetBuffer(), result_c->GetBuffer(), k_result_bytes);
            }
            buffer.End();

            const Fence poll_fence = Fence::Create(context.GetDeviceInterface(), false, "submission path poll");
            Check(
                context.WaitForFence(poll_fence.GetHandle(), 0), "a wait with nothing outstanding is already satisfied"
            );

            Submission submission{};
            submission.command_buffers.push_back(buffer.GetHandle());
            context.Submit(submission, poll_fence.GetHandle());
            Check(
                context.WaitForFence(poll_fence.GetHandle(), 500'000'000),
                "a finite wait budget completes the submitted work"
            );
            Check(
                context.WaitForFence(poll_fence.GetHandle(), 0),
                "a wait after a completed submission is already satisfied"
            );
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "webgpu_submission_path_test threw: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "webgpu_submission_path_test FAILED with %d mismatch(es).\n", g_failures);
        return 1;
    }
    std::printf(
        "webgpu_submission_path_test PASSED: shared compute scopes, batched constant publication and "
        "dispatch validation behaved as specified.\n"
    );
    return 0;
}
