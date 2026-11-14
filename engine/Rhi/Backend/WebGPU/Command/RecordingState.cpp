#include "Rhi/Backend/WebGPU/Command/RecordingState.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"

#include <cstdint>
#include <unordered_map>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        /**
         * @brief Every recording state the process has, keyed by the handles that name it.
         *
         * A state is keyed twice: by the native encoder that records into it, and -
         * once the command buffer is finished - by the finished buffer that a
         * submission names. Both keys are removed before the state is destroyed, and
         * the native objects are released by their owners after that, so a recycled
         * pointer can never find a stale entry.
         */
        std::unordered_map<uintptr_t, RecordingState *> &Registry() {
            static std::unordered_map<uintptr_t, RecordingState *> registry;
            return registry;
        }

        uintptr_t KeyOf(const void *object) noexcept {
            return reinterpret_cast<uintptr_t>(object);
        }

        /// @brief The host-visible buffer that carries every constant block.
        struct ConstantRing {
            WGPUBuffer buffer{nullptr};
            WGPUBindGroupLayout layout{nullptr};
            WGPUBindGroup group{nullptr};
            size_t capacity{0};

            bool Ready() const noexcept {
                return buffer != nullptr && group != nullptr;
            }
        };

        ConstantRing &Ring() {
            static ConstantRing ring{};
            if (ring.Ready()) {
                return ring;
            }
            WGPUDevice device = RegisteredWgpuDevice();
            if (device == nullptr) {
                return ring;
            }

            const WGPUBindGroupLayoutEntry entry = ConstantBlockLayoutEntry();
            WGPUBindGroupLayoutDescriptor layout_desc{};
            layout_desc.entryCount = 1;
            layout_desc.entries = &entry;
            layout_desc.label = WGPUStringView{"Constant Block Layout", 23};
            ring.layout = wgpuDeviceCreateBindGroupLayout(device, &layout_desc);

            ring.capacity = 1u << 20; // 1 MiB: 4096 windows per command buffer.
            WGPUBufferDescriptor buffer_desc{};
            buffer_desc.size = ring.capacity;
            buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            buffer_desc.mappedAtCreation = false;
            buffer_desc.label = WGPUStringView{"Constant Ring", 13};
            ring.buffer = wgpuDeviceCreateBuffer(device, &buffer_desc);

            WGPUBindGroupEntry group_entry{};
            group_entry.binding = 0;
            group_entry.buffer = ring.buffer;
            group_entry.offset = 0;
            group_entry.size = k_constant_window;

            WGPUBindGroupDescriptor group_desc{};
            group_desc.layout = ring.layout;
            group_desc.entryCount = 1;
            group_desc.entries = &group_entry;
            group_desc.label = WGPUStringView{"Constant Block Bind Group", 25};
            ring.group = wgpuDeviceCreateBindGroup(device, &group_desc);
            return ring;
        }

        SubmissionCounters &Counters() noexcept {
            static SubmissionCounters counters{};
            return counters;
        }
    } // namespace

    ConstantRingRef GetConstantRing() {
        ConstantRing &ring = Ring();
        return ConstantRingRef{ring.group, ring.capacity};
    }

    RecordingState *CreateRecordingState(WGPUCommandEncoder encoder) {
        auto *state = new RecordingState{};
        state->encoder = encoder;
        if (encoder != nullptr) {
            Registry()[KeyOf(encoder)] = state;
        }
        return state;
    }

    void DestroyRecordingState(RecordingState *state) noexcept {
        if (state == nullptr) {
            return;
        }
        CloseOpenPass(*state);
        ForgetFinishedCommandBuffer(*state);
        if (state->encoder != nullptr) {
            Registry().erase(KeyOf(state->encoder));
        }
        delete state;
    }

    RecordingState *FindRecordingState(WGPUCommandEncoder encoder) noexcept {
        if (encoder == nullptr) {
            return nullptr;
        }
        const auto itr = Registry().find(KeyOf(encoder));
        return itr == Registry().end() ? nullptr : itr->second;
    }

    RecordingState *FindFinishedRecordingState(WGPUCommandBuffer buffer) noexcept {
        if (buffer == nullptr) {
            return nullptr;
        }
        const auto itr = Registry().find(KeyOf(buffer));
        return itr == Registry().end() ? nullptr : itr->second;
    }

    void RegisterFinishedCommandBuffer(RecordingState &state, WGPUCommandBuffer finished) noexcept {
        ForgetFinishedCommandBuffer(state);
        state.finished = finished;
        if (finished != nullptr) {
            Registry()[KeyOf(finished)] = &state;
        }
    }

    void ForgetFinishedCommandBuffer(RecordingState &state) noexcept {
        if (state.finished == nullptr) {
            return;
        }
        const auto itr = Registry().find(KeyOf(state.finished));
        if (itr != Registry().end() && itr->second == &state) {
            Registry().erase(itr);
        }
        state.finished = nullptr;
    }

    void CloseRenderPass(RecordingState &state) noexcept {
        if (state.render_pass == nullptr) {
            state.in_render_pass = false;
            return;
        }
        wgpuRenderPassEncoderEnd(state.render_pass);
        wgpuRenderPassEncoderRelease(state.render_pass);
        state.render_pass = nullptr;
        state.in_render_pass = false;
        state.pass_pipeline_set = false;
    }

    void CloseComputePass(RecordingState &state) noexcept {
        if (state.compute_pass == nullptr) {
            return;
        }
        wgpuComputePassEncoderEnd(state.compute_pass);
        wgpuComputePassEncoderRelease(state.compute_pass);
        state.compute_pass = nullptr;
    }

    void CloseOpenPass(RecordingState &state) noexcept {
        CloseRenderPass(state);
        CloseComputePass(state);
    }

    void ResetRecordingState(RecordingState &state, WGPUCommandEncoder encoder) noexcept {
        if (state.encoder != nullptr) {
            Registry().erase(KeyOf(state.encoder));
        }
        state.encoder = encoder;
        if (encoder != nullptr) {
            Registry()[KeyOf(encoder)] = &state;
        }
        CloseOpenPass(state);
        state.pass_pipeline_set = false;
        state.constant_staging.clear();
    }

    bool HasStagedConstants(const RecordingState &state) noexcept {
        return !state.constant_staging.empty();
    }

    void PublishStagedConstants(RecordingState &state) noexcept {
        if (state.constant_staging.empty()) {
            return;
        }
        ConstantRing &ring = Ring();
        WGPUQueue queue = RegisteredWgpuQueue();
        if (!ring.Ready() || queue == nullptr) {
            state.constant_staging.clear();
            return;
        }
        // One write for the whole contiguous range: window `n` sits at
        // `n * k_constant_window`, so the staged bytes are exactly the device range
        // starting at zero. A queue write copies the source before returning, which
        // is what makes recycling the host staging immediately afterwards safe.
        wgpuQueueWriteBuffer(queue, ring.buffer, 0, state.constant_staging.data(), state.constant_staging.size());
        CountConstantWrite(state.constant_staging.size() / k_constant_window);
    }

    void RecycleStagedConstants(RecordingState &state) noexcept {
        state.constant_staging.clear();
    }

    const SubmissionCounters &GetSubmissionCounters() noexcept {
        return Counters();
    }

    void ResetSubmissionCounters() noexcept {
        Counters() = SubmissionCounters{};
    }

    void CountComputePassOpened() noexcept {
        Counters().compute_passes++;
    }

    void CountDispatch() noexcept {
        Counters().dispatches++;
    }

    void CountConstantWrite(uint64_t windows) noexcept {
        Counters().constant_writes++;
        Counters().constant_windows += windows;
    }

    void CountQueueSubmit() noexcept {
        Counters().queue_submits++;
    }
} // namespace Engine::Rhi
