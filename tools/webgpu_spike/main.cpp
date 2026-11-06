/**
 * @file main.cpp
 * @brief Spike harness: the standard WebGPU C interface on a desktop runtime and in a browser.
 *
 * This is the measurement program behind task group 1 of the
 * `rhi-webgpu-backend` change, and the skeleton the WebGPU backend's device,
 * submission and presentation paths are grown from. It is deliberately one
 * file with no engine dependency: it uses nothing but `webgpu/webgpu.h`, so the
 * same source builds headlessly against a desktop implementation and into a
 * browser page through Emscripten.
 *
 * What it measures, in the order it runs:
 *
 * 1. Device creation, and the limits the adapter actually reports (task 1.5).
 * 2. A compute dispatch whose result is read back to the host, which exercises
 *    buffer creation, a bind group, a compute scope and the map/unmap state
 *    machine the mapped-window contract is built on.
 * 3. One triangle rendered and read back (task 1.4): off screen on the desktop,
 *    into a canvas in the browser.
 * 4. The cost of one compute scope per dispatch against one scope for many
 *    dispatches (task 1.3).
 * 5. Two dispatches sharing one compute scope where the second reads what the
 *    first wrote (task 1.5): records whether the target happened to order them.
 *
 * Every number it prints is copied into `spike-notes.md`; the program itself is
 * a harness, not a product path.
 */

#include <webgpu/webgpu.h>

#if !defined(__EMSCRIPTEN__) && defined(_WIN32)
#include <dawn/native/DawnNative.h>
#include <windows.h>
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace {
    /// @brief How many dispatches the scope-overhead measurement submits.
    constexpr uint32_t k_overhead_dispatch_count = 2000;

    /// @brief How many times the shared-scope ordering check repeats.
    constexpr uint32_t k_ordering_repeat_count = 64;

    /// @brief Side length in pixels of the render target the triangle is drawn into.
    constexpr uint32_t k_render_target_size = 64;

    /**
     * @brief Wrap a C string as a WebGPU string view.
     *
     * The handle borrows the characters; every call site passes a string
     * literal or a string that outlives the call.
     */
    WGPUStringView Sv(const char *text) {
        WGPUStringView view{};
        view.data = text;
        view.length = text == nullptr ? 0 : std::strlen(text);
        return view;
    }

    /// @brief Print a message the runtime attached to a failed asynchronous call.
    void PrintCallbackMessage(const char *what, WGPUStringView message) {
        if (message.length == 0 || message.data == nullptr) {
            std::printf("  [%s] failed with no message\n", what);
            return;
        }
        std::printf("  [%s] failed: %.*s\n", what, static_cast<int>(message.length), message.data);
    }

    /**
     * @brief Run the instance's event loop until a flag is set.
     *
     * This is the blocking pump the mapped-window contract keeps for tests and
     * load-time code: the browser cannot block, so the pump asks the instance
     * to process whatever is ready and yields, instead of waiting on a fence.
     */
    void PumpUntil(WGPUInstance instance, const bool &done, const char *what) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!done) {
            wgpuInstanceProcessEvents(instance);
            if (std::chrono::steady_clock::now() > deadline) {
                std::printf("  [%s] timed out while pumping events\n", what);
                return;
            }
        }
    }

    /// @brief Adapter request result shared with its callback.
    struct AdapterRequest {
        WGPUAdapter adapter{nullptr};
        bool done{false};
    };

    /// @brief Device request result shared with its callback.
    struct DeviceRequest {
        WGPUDevice device{nullptr};
        bool done{false};
    };

    /// @brief Buffer-map result shared with its callback.
    struct MapRequest {
        bool done{false};
        bool succeeded{false};
    };

    /// @brief Work-done result shared with its callback.
    struct WorkDoneRequest {
        bool done{false};
    };

    void OnAdapterRequested(
        WGPURequestAdapterStatus status,
        WGPUAdapter adapter,
        WGPUStringView message,
        void *userdata1,
        void * /*userdata2*/
    ) {
        auto &request = *static_cast<AdapterRequest *>(userdata1);
        if (status == WGPURequestAdapterStatus_Success) {
            request.adapter = adapter;
        } else {
            PrintCallbackMessage("requestAdapter", message);
        }
        request.done = true;
    }

    void OnDeviceRequested(
        WGPURequestDeviceStatus status,
        WGPUDevice device,
        WGPUStringView message,
        void *userdata1,
        void * /*userdata2*/
    ) {
        auto &request = *static_cast<DeviceRequest *>(userdata1);
        if (status == WGPURequestDeviceStatus_Success) {
            request.device = device;
        } else {
            PrintCallbackMessage("requestDevice", message);
        }
        request.done = true;
    }

    void OnBufferMapped(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void * /*userdata2*/) {
        auto &request = *static_cast<MapRequest *>(userdata1);
        if (status == WGPUMapAsyncStatus_Success) {
            request.succeeded = true;
        } else {
            PrintCallbackMessage("bufferMapAsync", message);
        }
        request.done = true;
    }

    void OnWorkDone(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata1, void * /*userdata2*/) {
        auto &request = *static_cast<WorkDoneRequest *>(userdata1);
        if (status != WGPUQueueWorkDoneStatus_Success) {
            PrintCallbackMessage("onSubmittedWorkDone", message);
        }
        request.done = true;
    }

    void OnUncapturedError(
        WGPUDevice const * /*device*/,
        WGPUErrorType type,
        WGPUStringView message,
        void * /*userdata1*/,
        void * /*userdata2*/
    ) {
        std::printf(
            "  [uncaptured error type=%d] %.*s\n", static_cast<int>(type), static_cast<int>(message.length), message.data
        );
    }

    /// @brief Result of a validation error scope, shared with its callback.
    struct ErrorScopeRequest {
        bool done{false};
        WGPUErrorType type{WGPUErrorType_NoError};
        std::string message{};
    };

    void OnErrorScopePopped(
        WGPUPopErrorScopeStatus status,
        WGPUErrorType type,
        WGPUStringView message,
        void *userdata1,
        void * /*userdata2*/
    ) {
        auto &request = *static_cast<ErrorScopeRequest *>(userdata1);
        if (status != WGPUPopErrorScopeStatus_Success) {
            request.type = WGPUErrorType_Internal;
            request.message = "the error scope did not pop";
        } else {
            request.type = type;
            if (message.length != 0 && message.data != nullptr) {
                request.message.assign(message.data, message.length);
            }
        }
        request.done = true;
    }

    /**
     * @brief Open a validation error scope.
     *
     * The standard interface reports validation failures either to the
     * device-wide handler or to the innermost scope; a scope is what attributes
     * a failure to the operation that caused it, which is the reporting shape
     * the backend needs (task 8.6).
     */
    void PushValidationScope(WGPUDevice device) {
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    }

    /// @brief Close the innermost scope and describe what failed inside it, if anything.
    std::string PopValidationScope(WGPUInstance instance, WGPUDevice device) {
        ErrorScopeRequest request{};
        WGPUPopErrorScopeCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = OnErrorScopePopped;
        info.userdata1 = &request;
        wgpuDevicePopErrorScope(device, info);
        PumpUntil(instance, request.done, "popErrorScope");
        if (request.type == WGPUErrorType_NoError) {
            return {};
        }
        return request.message.empty() ? std::string{"validation failed with no message"} : request.message;
    }

    /// @brief Wait for the queue's submitted work to complete.
    void WaitForQueue(WGPUInstance instance, WGPUQueue queue) {
        WorkDoneRequest request{};
        WGPUQueueWorkDoneCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = OnWorkDone;
        info.userdata1 = &request;
        wgpuQueueOnSubmittedWorkDone(queue, info);
        PumpUntil(instance, request.done, "onSubmittedWorkDone");
    }

    /// @brief Create a shader module from WGSL source.
    WGPUShaderModule CreateShaderModule(WGPUDevice device, const char *label, const char *source) {
        WGPUShaderModuleDescriptor descriptor{};
        descriptor.label = Sv(label);
        WGPUShaderSourceWGSL source_desc{};
        source_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
        source_desc.code = Sv(source);
        descriptor.nextInChain = &source_desc.chain;
        return wgpuDeviceCreateShaderModule(device, &descriptor);
    }

    /// @brief Create a buffer with a fixed usage, zero-initialized by the caller as needed.
    WGPUBuffer CreateBuffer(WGPUDevice device, const char *label, uint64_t size, WGPUBufferUsage usage) {
        WGPUBufferDescriptor descriptor{};
        descriptor.label = Sv(label);
        descriptor.size = size;
        descriptor.usage = usage;
        descriptor.mappedAtCreation = false;
        return wgpuDeviceCreateBuffer(device, &descriptor);
    }

    /// @brief Read a buffer back into host memory through the map/unmap state machine.
    std::vector<uint32_t> ReadBack(WGPUInstance instance, WGPUBuffer buffer, uint64_t size) {
        MapRequest request{};
        WGPUBufferMapCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = OnBufferMapped;
        info.userdata1 = &request;
        wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, static_cast<size_t>(size), info);
        PumpUntil(instance, request.done, "bufferMapAsync");

        std::vector<uint32_t> words;
        if (!request.succeeded) {
            return words;
        }
        const auto *mapped = static_cast<const uint32_t *>(wgpuBufferGetConstMappedRange(buffer, 0, size));
        if (mapped != nullptr) {
            words.assign(mapped, mapped + size / sizeof(uint32_t));
        }
        wgpuBufferUnmap(buffer);
        return words;
    }

    /// @brief Print the limits that decide the backend's bind-group and buffer strategy.
    void ReportLimits(WGPUAdapter adapter) {
        WGPULimits limits{};
        if (wgpuAdapterGetLimits(adapter, &limits) != WGPUStatus_Success) {
            std::printf("limits: unavailable (adapter reports none)\n");
            return;
        }
        std::printf("limits:\n");
        std::printf("  maxBindGroups                    %u\n", limits.maxBindGroups);
        std::printf("  maxBindingsPerBindGroup          %u\n", limits.maxBindingsPerBindGroup);
        std::printf("  maxBindGroupsPlusVertexBuffers   %u\n", limits.maxBindGroupsPlusVertexBuffers);
        std::printf("  maxBufferSize                    %llu\n", static_cast<unsigned long long>(limits.maxBufferSize));
        std::printf(
            "  maxStorageBufferBindingSize      %llu\n",
            static_cast<unsigned long long>(limits.maxStorageBufferBindingSize)
        );
        std::printf(
            "  maxUniformBufferBindingSize      %llu\n",
            static_cast<unsigned long long>(limits.maxUniformBufferBindingSize)
        );
        std::printf("  maxComputeWorkgroupsPerDimension %u\n", limits.maxComputeWorkgroupsPerDimension);
        std::printf("  maxComputeInvocationsPerWorkgroup %u\n", limits.maxComputeInvocationsPerWorkgroup);
        std::printf("  maxTextureDimension2D            %u\n", limits.maxTextureDimension2D);
        std::printf(
            "  minUniformBufferOffsetAlignment  %u\n", static_cast<unsigned>(limits.minUniformBufferOffsetAlignment)
        );
        std::printf("  minStorageBufferOffsetAlignment  %u\n", limits.minStorageBufferOffsetAlignment);
    }

    /**
     * @brief Dispatch a kernel that writes `index * 2 + 1` into a storage buffer, and read it back.
     *
     * Exercises the whole host-to-device-to-host loop the backend needs: a
     * storage buffer, an auto-generated pipeline layout, one compute scope, and
     * the mapped readback path.
     */
    bool RunComputeSmokeTest(WGPUInstance instance, WGPUDevice device) {
        static constexpr const char *k_kernel = R"(
@group(0) @binding(0) var<storage, read_write> data: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x < arrayLength(&data)) {
        data[id.x] = id.x * 2u + 1u;
    }
}
)";
        constexpr uint32_t k_element_count = 256;
        constexpr uint64_t k_byte_size = k_element_count * sizeof(uint32_t);

        WGPUShaderModule module = CreateShaderModule(device, "spike.compute", k_kernel);
        WGPUComputePipelineDescriptor pipeline_desc{};
        pipeline_desc.label = Sv("spike.compute.pipeline");
        pipeline_desc.layout = nullptr;
        pipeline_desc.compute.module = module;
        pipeline_desc.compute.entryPoint = Sv("main");
        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &pipeline_desc);
        if (pipeline == nullptr) {
            std::printf("compute: pipeline creation failed\n");
            return false;
        }
        WGPUBindGroupLayout bind_group_layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

        WGPUBuffer buffer =
            CreateBuffer(device, "spike.compute.data", k_byte_size, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBuffer readback = CreateBuffer(
            device, "spike.compute.readback", k_byte_size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead
        );
        if (buffer == nullptr || readback == nullptr) {
            std::printf("compute: buffer creation failed\n");
            return false;
        }

        WGPUBindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = buffer;
        entry.offset = 0;
        entry.size = k_byte_size;
        WGPUBindGroupDescriptor bind_group_desc{};
        bind_group_desc.label = Sv("spike.compute.bindgroup");
        bind_group_desc.layout = bind_group_layout;
        bind_group_desc.entryCount = 1;
        bind_group_desc.entries = &entry;
        WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &bind_group_desc);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        WGPUComputePassDescriptor pass_desc{};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &pass_desc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, (k_element_count + 63) / 64, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer, 0, readback, 0, k_byte_size);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(wgpuDeviceGetQueue(device), 1, &commands);
        WaitForQueue(instance, wgpuDeviceGetQueue(device));

        const std::vector<uint32_t> words = ReadBack(instance, readback, k_byte_size);
        bool matches = words.size() == k_element_count;
        for (uint32_t index = 0; matches && index < k_element_count; ++index) {
            if (words[index] != index * 2 + 1) {
                matches = false;
                std::printf(
                    "compute: element %u is %u, expected %u\n", index, words[index], index * 2 + 1
                );
            }
        }
        std::printf(
            "compute: %s (%zu of %u elements read back)\n",
            matches ? "PASS" : "FAIL",
            words.size(),
            k_element_count
        );

        wgpuComputePassEncoderRelease(pass);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bind_group);
        wgpuBufferRelease(readback);
        wgpuBufferRelease(buffer);
        wgpuBindGroupLayoutRelease(bind_group_layout);
        wgpuComputePipelineRelease(pipeline);
        wgpuShaderModuleRelease(module);
        return matches;
    }

    /// @brief Draw one triangle and read back the pixel at the centre of the target.
    bool RunTriangleTest(WGPUInstance instance, WGPUDevice device, WGPUTextureView color_view) {
        static constexpr const char *k_triangle = R"(
@vertex
fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
    var corners = array<vec2<f32>, 3>(
        vec2<f32>(-0.75, -0.75),
        vec2<f32>( 0.75, -0.75),
        vec2<f32>( 0.0,   0.75)
    );
    return vec4<f32>(corners[index], 0.0, 1.0);
}

@fragment
fn fs() -> @location(0) vec4<f32> {
    return vec4<f32>(0.25, 0.5, 0.75, 1.0);
}
)";
        WGPUShaderModule module = CreateShaderModule(device, "spike.triangle", k_triangle);

        WGPUColorTargetState color_target{};
        color_target.format = WGPUTextureFormat_RGBA8Unorm;
        color_target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = Sv("fs");
        fragment.targetCount = 1;
        fragment.targets = &color_target;

        WGPURenderPipelineDescriptor pipeline_desc{};
        pipeline_desc.label = Sv("spike.triangle.pipeline");
        pipeline_desc.vertex.module = module;
        pipeline_desc.vertex.entryPoint = Sv("vs");
        pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeline_desc.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline_desc.primitive.cullMode = WGPUCullMode_None;
        pipeline_desc.multisample.count = 1;
        // A zero mask keeps every sample out of the rasteriser: the field is a
        // coverage mask, not a boolean, so it has to be all ones. Zero-valued
        // defaults are why a backend must use the `_INIT` macros rather than
        // value-initialising these descriptors.
        pipeline_desc.multisample.mask = 0xFFFFFFFFu;
        pipeline_desc.fragment = &fragment;
        PushValidationScope(device);
        WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);
        if (pipeline == nullptr) {
            std::printf("triangle: pipeline creation failed\n");
            const std::string error = PopValidationScope(instance, device);
            if (!error.empty()) {
                std::printf("triangle: pipeline error: %s\n", error.c_str());
            }
            return false;
        }

        WGPURenderPassColorAttachment color_attachment{};
        color_attachment.view = color_view;
        // A 2D attachment has no array slice; the field is only meaningful for a
        // 3D view, and the runtime rejects a zero slice outright.
        color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_attachment.loadOp = WGPULoadOp_Clear;
        color_attachment.storeOp = WGPUStoreOp_Store;
        // A black clear, so the non-black pixel count is exactly the area the
        // triangle covered.
        color_attachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};
        WGPURenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments = &color_attachment;

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(wgpuDeviceGetQueue(device), 1, &commands);
        WaitForQueue(instance, wgpuDeviceGetQueue(device));

        const std::string error = PopValidationScope(instance, device);
        if (error.empty()) {
            std::printf("triangle: submitted, no validation error\n");
        } else {
            std::printf("triangle: validation error: %s\n", error.c_str());
        }

        wgpuRenderPassEncoderRelease(pass);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        wgpuRenderPipelineRelease(pipeline);
        wgpuShaderModuleRelease(module);
        return true;
    }

    /**
     * @brief Compare one compute scope per dispatch against one scope for all of them.
     *
     * Both arms submit the same number of dispatches of the same kernel, so the
     * difference is the per-scope cost the backend pays for ordering by scope
     * boundary instead of by barrier (task 1.3).
     */
    void MeasureScopeOverhead(WGPUInstance instance, WGPUDevice device) {
        static constexpr const char *k_kernel = R"(
@group(0) @binding(0) var<storage, read_write> data: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x < arrayLength(&data)) {
        data[id.x] = data[id.x] + 1u;
    }
}
)";
        constexpr uint32_t k_element_count = 1024;
        constexpr uint64_t k_byte_size = k_element_count * sizeof(uint32_t);

        WGPUShaderModule module = CreateShaderModule(device, "spike.overhead", k_kernel);
        WGPUComputePipelineDescriptor pipeline_desc{};
        pipeline_desc.layout = nullptr;
        pipeline_desc.compute.module = module;
        pipeline_desc.compute.entryPoint = Sv("main");
        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &pipeline_desc);
        WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBuffer buffer =
            CreateBuffer(device, "spike.overhead.data", k_byte_size, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = buffer;
        entry.size = k_byte_size;
        WGPUBindGroupDescriptor bind_group_desc{};
        bind_group_desc.layout = layout;
        bind_group_desc.entryCount = 1;
        bind_group_desc.entries = &entry;
        WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &bind_group_desc);

        const auto measure = [&](bool one_scope_per_dispatch) {
            const auto start = std::chrono::steady_clock::now();
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
            if (one_scope_per_dispatch) {
                for (uint32_t index = 0; index < k_overhead_dispatch_count; ++index) {
                    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
                    wgpuComputePassEncoderSetPipeline(pass, pipeline);
                    wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
                    wgpuComputePassEncoderDispatchWorkgroups(pass, 4, 1, 1);
                    wgpuComputePassEncoderEnd(pass);
                    wgpuComputePassEncoderRelease(pass);
                }
            } else {
                WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
                for (uint32_t index = 0; index < k_overhead_dispatch_count; ++index) {
                    wgpuComputePassEncoderDispatchWorkgroups(pass, 4, 1, 1);
                }
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }
            WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
            wgpuQueueSubmit(wgpuDeviceGetQueue(device), 1, &commands);
            WaitForQueue(instance, wgpuDeviceGetQueue(device));
            const auto elapsed = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - start
            )
                                     .count();
            wgpuCommandBufferRelease(commands);
            wgpuCommandEncoderRelease(encoder);
            return elapsed;
        };

        const double batched_ms = measure(false);
        const double split_ms = measure(true);
        std::printf(
            "scope overhead: %u dispatches, one scope %.2f ms, one scope per dispatch %.2f ms, per-scope cost "
            "%.3f us\n",
            k_overhead_dispatch_count,
            batched_ms,
            split_ms,
            (split_ms - batched_ms) * 1000.0 / k_overhead_dispatch_count
        );

        wgpuBindGroupRelease(bind_group);
        wgpuBufferRelease(buffer);
        wgpuBindGroupLayoutRelease(layout);
        wgpuComputePipelineRelease(pipeline);
        wgpuShaderModuleRelease(module);
    }

    /**
     * @brief Record whether two dispatches sharing one compute scope are ordered.
     *
     * The second dispatch reads the word the first one writes. WGSL gives no
     * ordering *inside* a compute scope, so a correct counter that always ends
     * up right means the target happens to serialize dispatches; the
     * per-dispatch-scope design does not rely on that either way, which is why
     * this is a measurement and not an assertion.
     */
    void MeasureSharedScopeOrdering(WGPUInstance instance, WGPUDevice device) {
        static constexpr const char *k_kernel = R"(
@group(0) @binding(0) var<storage, read_write> stage: u32;
@group(0) @binding(1) var<storage, read_write> result: u32;

@compute @workgroup_size(1)
fn write(@builtin(global_invocation_id) id: vec3<u32>) {
    stage = 7u;
}

@compute @workgroup_size(1)
fn read(@builtin(global_invocation_id) id: vec3<u32>) {
    result = stage + 1u;
}
)";
        WGPUShaderModule module = CreateShaderModule(device, "spike.ordering", k_kernel);
        // An explicit layout, because the two entry points use the two bindings
        // in opposite roles and only an explicit layout guarantees that both
        // pipelines accept the same bind group.
        WGPUBindGroupLayoutEntry layout_entries[2]{};
        layout_entries[0].binding = 0;
        layout_entries[0].visibility = WGPUShaderStage_Compute;
        layout_entries[0].buffer.type = WGPUBufferBindingType_Storage;
        layout_entries[1].binding = 1;
        layout_entries[1].visibility = WGPUShaderStage_Compute;
        layout_entries[1].buffer.type = WGPUBufferBindingType_Storage;
        WGPUBindGroupLayoutDescriptor layout_desc{};
        layout_desc.entryCount = 2;
        layout_desc.entries = layout_entries;
        WGPUBindGroupLayout bind_group_layout = wgpuDeviceCreateBindGroupLayout(device, &layout_desc);
        WGPUPipelineLayoutDescriptor pipeline_layout_desc{};
        pipeline_layout_desc.bindGroupLayoutCount = 1;
        pipeline_layout_desc.bindGroupLayouts = &bind_group_layout;
        WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_desc);

        WGPUComputePipelineDescriptor write_desc{};
        write_desc.layout = pipeline_layout;
        write_desc.compute.module = module;
        write_desc.compute.entryPoint = Sv("write");
        WGPUComputePipeline write_pipeline = wgpuDeviceCreateComputePipeline(device, &write_desc);
        WGPUComputePipelineDescriptor read_desc{};
        read_desc.layout = pipeline_layout;
        read_desc.compute.module = module;
        read_desc.compute.entryPoint = Sv("read");
        WGPUComputePipeline read_pipeline = wgpuDeviceCreateComputePipeline(device, &read_desc);
        if (write_pipeline == nullptr || read_pipeline == nullptr) {
            std::printf("ordering: pipeline creation failed\n");
            return;
        }

        WGPUBuffer stage = CreateBuffer(
            device,
            "spike.ordering.stage",
            sizeof(uint32_t),
            // COPY_DST, because `wgpuQueueWriteBuffer` writes through a copy:
            // a queue write is not a mapped write.
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst
        );
        WGPUBuffer result = CreateBuffer(
            device,
            "spike.ordering.result",
            sizeof(uint32_t),
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst
        );
        WGPUBuffer readback = CreateBuffer(
            device, "spike.ordering.readback", sizeof(uint32_t), WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead
        );

        // Both entry points take the same bind group: the auto-generated layout
        // of a module covers every binding the module declares, whichever entry
        // point a pipeline uses, so one group serves both pipelines.
        WGPUBindGroupEntry binding_entries[2]{};
        binding_entries[0].binding = 0;
        binding_entries[0].buffer = stage;
        binding_entries[0].size = sizeof(uint32_t);
        binding_entries[1].binding = 1;
        binding_entries[1].buffer = result;
        binding_entries[1].size = sizeof(uint32_t);
        WGPUBindGroupDescriptor bind_desc{};
        bind_desc.layout = bind_group_layout;
        bind_desc.entryCount = 2;
        bind_desc.entries = binding_entries;
        WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &bind_desc);

        uint32_t ordered_count = 0;
        for (uint32_t repeat = 0; repeat < k_ordering_repeat_count; ++repeat) {
            const uint32_t zero = 0;
            wgpuQueueWriteBuffer(wgpuDeviceGetQueue(device), stage, 0, &zero, sizeof(zero));
            wgpuQueueWriteBuffer(wgpuDeviceGetQueue(device), result, 0, &zero, sizeof(zero));

            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, write_pipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderSetPipeline(pass, read_pipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, result, 0, readback, 0, sizeof(uint32_t));
            WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
            wgpuQueueSubmit(wgpuDeviceGetQueue(device), 1, &commands);
            WaitForQueue(instance, wgpuDeviceGetQueue(device));
            const std::vector<uint32_t> words = ReadBack(instance, readback, sizeof(uint32_t));
            if (!words.empty() && words[0] == 8) {
                ++ordered_count;
            }
            wgpuCommandBufferRelease(commands);
            wgpuCommandEncoderRelease(encoder);
            wgpuComputePassEncoderRelease(pass);
        }
        std::printf(
            "shared scope ordering: %u of %u runs observed the earlier dispatch (unordered is permitted)\n",
            ordered_count,
            k_ordering_repeat_count
        );

        wgpuBindGroupRelease(bind_group);
        wgpuPipelineLayoutRelease(pipeline_layout);
        wgpuBindGroupLayoutRelease(bind_group_layout);
        wgpuBufferRelease(readback);
        wgpuBufferRelease(result);
        wgpuBufferRelease(stage);
        wgpuComputePipelineRelease(read_pipeline);
        wgpuComputePipelineRelease(write_pipeline);
        wgpuShaderModuleRelease(module);
    }

    /// @brief Per-run state the browser entry point keeps alive.
    struct RunContext {
        WGPUInstance instance{nullptr};
        WGPUAdapter adapter{nullptr};
        WGPUDevice device{nullptr};
        /// Off-screen colour target, used by the desktop shape and by the browser
        /// until its canvas surface is configured.
        WGPUTexture offscreen{nullptr};
        WGPUTextureView offscreen_view{nullptr};
        WGPUSurface surface{nullptr};
        bool finished{false};
    };

    /// @brief Create the off-screen colour target the triangle is drawn into.
    bool CreateOffscreenTarget(WGPUDevice device, RunContext &context) {
        WGPUTextureDescriptor descriptor{};
        descriptor.label = Sv("spike.offscreen");
        descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size.width = k_render_target_size;
        descriptor.size.height = k_render_target_size;
        descriptor.size.depthOrArrayLayers = 1;
        descriptor.format = WGPUTextureFormat_RGBA8Unorm;
        descriptor.mipLevelCount = 1;
        descriptor.sampleCount = 1;
        context.offscreen = wgpuDeviceCreateTexture(device, &descriptor);
        if (context.offscreen == nullptr) {
            return false;
        }
        context.offscreen_view = wgpuTextureCreateView(context.offscreen, nullptr);
        return context.offscreen_view != nullptr;
    }

    /// @brief Read the centre pixel of the off-screen target back to the host.
    void ReadBackCentrePixel(WGPUInstance instance, WGPUDevice device, WGPUTexture source) {
        // WebGPU requires the copy row pitch to be a multiple of 256 bytes, which
        // is why the target is 64 pixels of RGBA8 wide: 64 * 4 == 256.
        constexpr uint64_t k_row_bytes = k_render_target_size * 4;
        WGPUBuffer readback =
            CreateBuffer(device, "spike.triangle.readback", k_row_bytes * k_render_target_size, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        WGPUTexelCopyTextureInfo source_info{};
        source_info.texture = source;
        WGPUTexelCopyBufferInfo destination_info{};
        destination_info.buffer = readback;
        destination_info.layout.bytesPerRow = static_cast<uint32_t>(k_row_bytes);
        destination_info.layout.rowsPerImage = k_render_target_size;
        WGPUExtent3D extent{};
        extent.width = k_render_target_size;
        extent.height = k_render_target_size;
        extent.depthOrArrayLayers = 1;
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source_info, &destination_info, &extent);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(wgpuDeviceGetQueue(device), 1, &commands);
        WaitForQueue(instance, wgpuDeviceGetQueue(device));

        const std::vector<uint32_t> words =
            ReadBack(instance, readback, k_row_bytes * k_render_target_size);
        // How much of the target the triangle covered, which is what tells a
        // "nothing was drawn" result apart from a wrong pixel offset.
        size_t non_black = 0;
        for (const uint32_t word : words) {
            if ((word & 0x00FFFFFFu) != 0) {
                ++non_black;
            }
        }
        std::printf("triangle: %zu of %zu pixels are non-black\n", non_black, words.size());
        const size_t centre = (k_render_target_size / 2) * (k_row_bytes / 4) + k_render_target_size / 2;
        if (words.size() > centre) {
            const uint32_t packed = words[centre];
            std::printf(
                "triangle: centre pixel rgba = (%u, %u, %u, %u)\n",
                packed & 0xFFu,
                (packed >> 8) & 0xFFu,
                (packed >> 16) & 0xFFu,
                (packed >> 24) & 0xFFu
            );
        }
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        wgpuBufferRelease(readback);
    }

    void RunMeasurements(RunContext &context) {
        std::printf("adapter: %s\n", "acquired");
        ReportLimits(context.adapter);
        if (!CreateOffscreenTarget(context.device, context)) {
            std::printf("offscreen target: creation failed\n");
            context.finished = true;
            return;
        }
        RunComputeSmokeTest(context.instance, context.device);
        RunTriangleTest(context.instance, context.device, context.offscreen_view);
        ReadBackCentrePixel(context.instance, context.device, context.offscreen);
        MeasureScopeOverhead(context.instance, context.device);
        MeasureSharedScopeOrdering(context.instance, context.device);
        context.finished = true;
    }

#ifdef __EMSCRIPTEN__
    /// @brief Configure a canvas surface and draw the triangle into it.
    bool ConfigureCanvasSurface(RunContext &context) {
        WGPUSurfaceSourceCanvasHTMLSelector canvas_source{};
        canvas_source.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector;
        canvas_source.selector = Sv("#webgpu-canvas");
        WGPUSurfaceDescriptor surface_desc{};
        surface_desc.nextInChain = &canvas_source.chain;
        context.surface = wgpuInstanceCreateSurface(context.instance, &surface_desc);
        if (context.surface == nullptr) {
            std::printf("canvas surface: creation failed\n");
            return false;
        }
        WGPUSurfaceConfiguration config{};
        config.device = context.device;
        config.format = wgpuSurfaceGetPreferredFormat(context.surface, context.adapter);
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.width = k_render_target_size;
        config.height = k_render_target_size;
        config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(context.surface, &config);
        WGPUTexture surface_texture = wgpuSurfaceGetCurrentTexture(context.surface);
        WGPUTextureView view = wgpuTextureCreateView(surface_texture, nullptr);
        RunTriangleTest(context.instance, context.device, view);
        wgpuTextureViewRelease(view);
        wgpuSurfacePresent(context.surface);
        return true;
    }
#endif

    WGPUInstance CreateWebGPUInstance() {
        WGPUInstanceDescriptor instance_desc{};
#if !defined(__EMSCRIPTEN__) && defined(_WIN32)
        std::array<char, MAX_PATH> system_directory{};
        const UINT length = GetSystemDirectoryA(system_directory.data(), system_directory.size());
        if (length == 0 || length >= system_directory.size()) {
            std::printf("instance: cannot locate the Windows system directory\n");
            return nullptr;
        }
        std::string system_path(system_directory.data(), length);
        system_path.push_back('\\');
        std::array<std::string_view, 1> search_paths{system_path};
        dawn::native::DawnInstanceDescriptor dawn_desc{};
        dawn_desc.additionalRuntimeSearchPaths = search_paths;
        instance_desc.nextInChain = reinterpret_cast<WGPUChainedStruct *>(&dawn_desc);
#endif
        return wgpuCreateInstance(&instance_desc);
    }

    /**
     * @brief Create the instance, adapter and device, then run the measurements.
     *
     * Adapter and device requests are asynchronous in the standard interface;
     * the pump drives them to completion without blocking on a fence, which is
     * the shape the backend uses.
     */
    int Run() {
        RunContext context{};
        context.instance = CreateWebGPUInstance();
        if (context.instance == nullptr) {
            std::printf("instance: creation failed\n");
            return 1;
        }

        AdapterRequest adapter_request{};
        WGPURequestAdapterOptions adapter_options{};
        adapter_options.powerPreference = WGPUPowerPreference_HighPerformance;
        WGPURequestAdapterCallbackInfo adapter_callback{};
        adapter_callback.mode = WGPUCallbackMode_AllowProcessEvents;
        adapter_callback.callback = OnAdapterRequested;
        adapter_callback.userdata1 = &adapter_request;
        wgpuInstanceRequestAdapter(context.instance, &adapter_options, adapter_callback);
        PumpUntil(context.instance, adapter_request.done, "requestAdapter");
        context.adapter = adapter_request.adapter;
        if (context.adapter == nullptr) {
            std::printf("adapter: none available\n");
            return 1;
        }

        DeviceRequest device_request{};
        WGPUDeviceDescriptor device_desc{};
        device_desc.label = Sv("spike.device");
        device_desc.uncapturedErrorCallbackInfo.callback = OnUncapturedError;
        WGPURequestDeviceCallbackInfo device_callback{};
        device_callback.mode = WGPUCallbackMode_AllowProcessEvents;
        device_callback.callback = OnDeviceRequested;
        device_callback.userdata1 = &device_request;
        wgpuAdapterRequestDevice(context.adapter, &device_desc, device_callback);
        PumpUntil(context.instance, device_request.done, "requestDevice");
        context.device = device_request.device;
        if (context.device == nullptr) {
            std::printf("device: creation failed\n");
            return 1;
        }

#ifdef __EMSCRIPTEN__
        ConfigureCanvasSurface(context);
#endif
        RunMeasurements(context);
        std::printf("spike: done\n");
        return 0;
    }
} // namespace

int main() {
    // Unbuffered, so a runtime panic in a later step cannot swallow the measurements already printed.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const int result = Run();
#ifdef __EMSCRIPTEN__
    // The browser keeps the page alive; nothing else to do after the run.
    emscripten_force_exit(result);
#endif
    return result;
}
