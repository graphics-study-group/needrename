#include "Rhi/Pipeline/ComputeKernel.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Pipeline/ShaderInterfaceDescription.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Texture/Texture.h"
#include "Rhi/Texture/TextureSubresourceView.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <map>
#include <string_view>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    /**
     * @brief A compute pipeline and the bindings it was built around.
     *
     * The Vulkan kernel reflects its module and derives the binding table from that. This
     * one is handed the table by the bake, which is the point of D9: no reflection library
     * is linked into the build that runs here.
     *
     * `parameter_block_size` is the size the bake recorded for the kernel's constant block,
     * and it is bound as a dynamic uniform window rather than as push constants, which is
     * what this backend substitutes for them. A caller that dispatches with a value gets the
     * value into the shader; a caller that dispatches without one is refused rather than
     * having the block read whatever the window held.
     */
    struct ComputeKernel::impl {
        DeviceContext *device_context{nullptr};
        std::string name{};
        uint32_t parameter_block_size{0};
        PipelineHandle pipeline{};
        PipelineLayoutHandle pipeline_layout{};
        DescriptorSetLayoutHandle set_layout{};
        DescriptorSetLayoutRef set_layout_ref{};
        std::map<uint32_t, DescriptorSetLayoutRef> set_layouts_by_index;
        ShaderInterfaceDescription description{};
        /// Name to the description entry that declares it, for matching a dispatch.
        ///
        /// The comparator is transparent so a dispatch can look up the `std::string_view` its
        /// dictionary is keyed by directly: a `std::less<void>` map is searched without building
        /// a temporary `std::string` for every name of every dispatch.
        std::map<std::string, const ShaderInterfaceBinding *, std::less<>> interface_by_name{};
        /// A set with nothing bound, used for the groups a padded pipeline layout declares.
        ///
        /// The constant block lives at group 3, so groups 1 and 2 exist in every kernel's
        /// pipeline layout whether or not the kernel has anything to put there - and the
        /// interface requires every group of a layout to be bound before a dispatch. This is
        /// what those groups get.
        DescriptorSetLayoutHandle empty_set_layout{};
        /// @brief Whether the device built this kernel's pipeline.
        ///
        /// True until a creation says otherwise: a kernel that reaches a dispatch without having
        /// gone through `CreateFromModule` has not been refused by anything, and silently skipping
        /// its work would turn a missing call into a black frame.
        bool available{true};
    };

    ComputeKernelResource::ComputeKernelResource(const DeviceBuffer &buffer) {
        kind = Kind::Buffer;
        this->buffer.ref = buffer.GetBufferRef();
        this->buffer.offset = 0u;
        this->buffer.size = std::numeric_limits<size_t>::max();
    }

    ComputeKernelResource ComputeKernelResource::Buffer(const DeviceBuffer &buffer, size_t offset, size_t size) {
        ComputeKernelResource resource(buffer);
        resource.buffer.offset = offset;
        resource.buffer.size = size;
        return resource;
    }

    ComputeKernelResource ComputeKernelResource::Image(Texture &texture, const TextureSubresourceRange &range) {
        ComputeKernelResource resource;
        resource.kind = Kind::Texture;
        resource.image.view = texture.GetViewRef(range);
        resource.image.sampler = texture.GetSamplerRef();
        return resource;
    }

    ComputeKernel::ComputeKernel(DeviceContext &device_context) : pimpl(std::make_unique<impl>()) {
        pimpl->device_context = &device_context;
    }

    ComputeKernel::~ComputeKernel() = default;

    void ComputeKernel::CreateFromModule(
        ShaderModuleHandle module, const ShaderInterfaceDescription &description, std::string_view name
    ) {
        pimpl->name = std::string{name};
        pimpl->description = description;
        // A dispatch names its resources, so each declared interface needs to be findable by
        // name. The map's values point into the description the kernel now owns, which does not
        // move again.
        for (const ShaderInterfaceBinding &declared : pimpl->description.bindings) {
            pimpl->interface_by_name[declared.name] = &declared;
        }
        pimpl->empty_set_layout = pimpl->device_context->GetIRCache().GetDescriptorSetLayout(
            DescriptorSetLayout{}, "Empty Descriptor Set Layout"
        );
        // One set layout per set the description names. Grouping by set is the shape the
        // Vulkan kernel produces from its reflected table, so a shader that bakes the same
        // way binds the same way.
        std::map<uint32_t, DescriptorSetLayout> sets;
        for (const ShaderInterfaceBinding &binding : description.bindings) {
            DescriptorBinding entry{};
            entry.binding = binding.binding;
            entry.type = ShaderInterfaceDescription::ToDescriptorType(binding.kind);
            entry.count = 1;
            // A storage image's format and access are what the shader declared them as; the
            // bind group can only satisfy them if the layout names the same pair.
            entry.storage_format = binding.storage_format;
            entry.storage_read_only = binding.storage_read_only;
            // A combined binding's sampler goes in the slot the bake recorded: the layout this
            // builds expands the pair into the two bindings the shader declares.
            entry.sampler_binding = binding.sampler_binding;
            sets[binding.set].push_back(entry);
        }

        ImmutableResourceCache &cache = pimpl->device_context->GetIRCache();
        std::vector<DescriptorSetLayoutHandle> set_layouts(k_constant_bind_group);
        for (auto &slot : set_layouts) {
            slot = cache.GetDescriptorSetLayoutRef(DescriptorSetLayout{}, "Empty Descriptor Set Layout", true).handle;
        }
        for (auto &[set_index, layout] : sets) {
            if (set_index >= k_constant_bind_group) {
                continue;
            }
            // One stage: these bindings belong to a compute pipeline, and a layout that also claims
            // the fragment stage is one the device may refuse for the number of storage buffers.
            const DescriptorSetLayoutRef resolved = cache.GetDescriptorSetLayoutRef(layout, name.data(), true);
            set_layouts[set_index] = resolved.handle;
            pimpl->set_layouts_by_index[set_index] = resolved;
            if (!pimpl->set_layout_ref.handle.IsValid()) {
                pimpl->set_layout_ref = resolved;
                pimpl->set_layout = resolved.handle;
            }
        }

        // A kernel that declares a constant block reads it from the group the bake moved it to,
        // so its pipeline layout has to declare that group. The seam spells "this pipeline has a
        // constant block" as a push-constant range, which is where the backend learns the size -
        // the block itself is a uniform buffer here.
        std::vector<Rhi::PushConstantRange> constant_ranges;
        if (description.has_constant_block) {
            // The size the caller is held to is the block the bake recorded: a kernel's `Dispatch`
            // asserts that what it pushes fits, and a size of zero would refuse every push.
            pimpl->parameter_block_size = description.constant_block.size;
            constant_ranges.push_back(Rhi::PushConstantRange{0, description.constant_block.size, true});
        }
        const PipelineLayoutDescription layout_description{
            .set_layouts = set_layouts, .push_constant_ranges = constant_ranges
        };
        pimpl->pipeline_layout = cache.GetPipelineLayout(layout_description, name.data());

        const WGPUDevice device = ToWgpuDevice(pimpl->device_context->GetDevice());
        WGPUComputePipelineDescriptor pipeline_desc{};
        pipeline_desc.layout = ToWgpuPipelineLayout(pimpl->pipeline_layout);
        pipeline_desc.compute.module = ToWgpuShaderModule(module);
        pipeline_desc.compute.entryPoint =
            WGPUStringView{description.entry_point.data(), description.entry_point.size()};
        if (!name.empty()) {
            pipeline_desc.label = WGPUStringView{name.data(), name.size()};
        }

        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &pipeline_desc);
        if (pipeline == nullptr) {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU: compute pipeline creation failed for '%.*s'.",
                static_cast<int>(name.size()),
                name.data()
            );
        }
        pimpl->pipeline = PipelineHandle::FromValue(reinterpret_cast<uintptr_t>(pipeline));
    }

    void ComputeKernel::Create(const std::vector<uint32_t> &, std::string_view) {
        // Reflecting SPIR-V is what this backend does not do; the bake already did it.
        throw std::logic_error(
            "ComputeKernel::Create takes SPIR-V words, which this backend cannot reflect. Use "
            "CreateFromModule with the interface description the bake produced."
        );
    }

    const std::string &ComputeKernel::GetName() const noexcept {
        return pimpl->name;
    }

    uint32_t ComputeKernel::GetPushConstantSize() const noexcept {
        return pimpl->parameter_block_size;
    }

    PipelineHandle ComputeKernel::GetPipeline() const noexcept {
        return pimpl->pipeline;
    }

    PipelineLayoutHandle ComputeKernel::GetPipelineLayout() const noexcept {
        return pimpl->pipeline_layout;
    }

    DescriptorSetLayoutHandle ComputeKernel::GetDescriptorSetLayout() const noexcept {
        return pimpl->set_layout;
    }

    void ComputeKernel::Dispatch(
        CommandEncoder &encoder,
        ComputeResourceDictionary resources,
        uint32_t group_count_x,
        uint32_t group_count_y,
        uint32_t group_count_z
    ) const {
        if (pimpl->parameter_block_size != 0) {
            throw std::logic_error("This kernel declares a parameter block; dispatch it with a value.");
        }
        DispatchAndRecord(encoder, resources, group_count_x, group_count_y, group_count_z, nullptr, 0);
    }

    void ComputeKernel::DispatchAndRecord(
        CommandEncoder &encoder,
        ComputeResourceDictionary resources,
        uint32_t group_count_x,
        uint32_t group_count_y,
        uint32_t group_count_z,
        const void *push,
        size_t push_size
    ) const {
        if (push != nullptr && push_size != 0 && !pimpl->description.has_constant_block) {
            throw std::logic_error("WebGPU: kernel was dispatched with a parameter block it does not declare.");
        }
        if (!pimpl->pipeline.IsValid()) {
            throw std::logic_error("WebGPU: dispatching a kernel that was never created.");
        }
        if (!pimpl->available) {
            // The device refused this kernel, so there is nothing to record. The refusal was
            // logged when the pipeline came back empty, so this path is silent.
            return;
        }

        // One record per supplied name: the declared interface it matched, plus the binding
        // content it resolved to. Resolving once is what removes the repeated work this used to
        // do - three passes over the dictionary, each building a `std::string` to look a name up.
        struct ResolvedRecord {
            const ShaderInterfaceBinding *declared{nullptr};
            ResolvedBinding binding{};
        };
        std::vector<ResolvedRecord> resolved;
        resolved.reserve(resources.size());
        for (const auto &[key, resource] : resources) {
            const auto itr = pimpl->interface_by_name.find(key);
            if (itr == pimpl->interface_by_name.end()) {
                throw std::runtime_error(
                    std::format("Compute kernel '{}' does not declare interface '{}'.", pimpl->name, key)
                );
            }
            const ShaderInterfaceBinding &declared = *itr->second;
            ResolvedRecord record{};
            record.declared = &declared;
            record.binding.binding = declared.binding;
            record.binding.type = ShaderInterfaceDescription::ToDescriptorType(declared.kind);
            const bool is_image = record.binding.type == DescriptorType::CombinedImageSampler
                                  || record.binding.type == DescriptorType::SampledImage
                                  || record.binding.type == DescriptorType::StorageImage
                                  || record.binding.type == DescriptorType::Sampler;
            if (is_image) {
                if (resource.kind != ComputeKernelResource::Kind::Texture) {
                    throw std::runtime_error(
                        std::format(
                            "Compute kernel '{}' interface '{}' is an image, but a buffer was supplied.",
                            pimpl->name,
                            key
                        )
                    );
                }
                record.binding.image_view = resource.image.view;
                record.binding.sampler = resource.image.sampler;
            } else {
                if (resource.kind != ComputeKernelResource::Kind::Buffer) {
                    throw std::runtime_error(
                        std::format(
                            "Compute kernel '{}' interface '{}' is a buffer, but an image was supplied.",
                            pimpl->name,
                            key
                        )
                    );
                }
                record.binding.buffer = resource.buffer.ref;
                record.binding.offset = resource.buffer.offset;
                record.binding.range = resource.buffer.size;
            }
            resolved.push_back(record);
        }

        // Order by set and then by binding, once. The arena keys a bind group on its content, so
        // the caller's dictionary order must not leak into the entries it is handed; ordering here
        // is also what makes each set's entries one contiguous span below.
        std::sort(resolved.begin(), resolved.end(), [](const ResolvedRecord &lhs, const ResolvedRecord &rhs) {
            if (lhs.declared->set != rhs.declared->set) {
                return lhs.declared->set < rhs.declared->set;
            }
            return lhs.declared->binding < rhs.declared->binding;
        });

        // WebGPU rejects a dispatch that reaches one buffer through two storage bindings when
        // either of them is writable ("writable storage buffer binding aliasing"). Vulkan accepts
        // it, and the previous desktop implementation accepted it too, so a browser-only rule
        // would otherwise stay undiscovered until the page runs it. Checking it here makes the
        // desktop validation build fail the same way the browser does.
        {
            // A device cannot offer more storage bindings than this in one dispatch, and the
            // entries are pointers into the interface description, so the scan costs no
            // allocation and builds a message only when it finds a conflict.
            constexpr size_t k_max_tracked_storage_uses = 64;
            constexpr std::string_view k_storage_buffer_kind{"storage-buffer"};
            struct StorageUse {
                uint64_t identity{0};
                const ShaderInterfaceBinding *declared{nullptr};
            };
            std::array<StorageUse, k_max_tracked_storage_uses> uses{};
            size_t use_count = 0;
            for (const ResolvedRecord &record : resolved) {
                const ShaderInterfaceBinding &declared = *record.declared;
                if (declared.kind != k_storage_buffer_kind) {
                    continue;
                }
                const uint64_t identity = record.binding.buffer.id.GetValue();
                for (size_t seen = 0; seen < use_count; seen++) {
                    if (uses[seen].identity != identity) {
                        continue;
                    }
                    const ShaderInterfaceBinding &earlier = *uses[seen].declared;
                    if (earlier.storage_read_only && declared.storage_read_only) {
                        // Two read-only bindings of one buffer are what every shader that reads
                        // the same data twice does, and WebGPU allows it.
                        continue;
                    }
                    throw std::runtime_error(
                        std::format(
                            "WebGPU: compute kernel '{}' binds one buffer (identity {}) to two storage "
                            "bindings of one dispatch - set {} binding {} ('{}') and set {} binding {} "
                            "('{}') - and at least one is writable. WebGPU forbids that aliasing; use "
                            "one read-write binding or two buffers.",
                            pimpl->name,
                            identity,
                            earlier.set,
                            earlier.binding,
                            earlier.name,
                            declared.set,
                            declared.binding,
                            declared.name
                        )
                    );
                }
                if (use_count < uses.size()) {
                    uses[use_count++] = StorageUse{identity, &declared};
                }
            }
        }

        // Re-acquire on every dispatch: the arena keys on content, so an identical dictionary
        // reuses one bind group and a changed one does not.
        encoder.BindComputePipeline(pimpl->pipeline, pimpl->pipeline_layout);

        std::vector<ResolvedBinding> bindings;
        bindings.reserve(resolved.size());
        for (const ResolvedRecord &record : resolved) {
            bindings.push_back(record.binding);
        }
        size_t group_begin = 0;
        while (group_begin < bindings.size()) {
            const uint32_t set_index = resolved[group_begin].declared->set;
            size_t group_end = group_begin + 1;
            while (group_end < bindings.size() && resolved[group_end].declared->set == set_index) {
                group_end++;
            }
            const auto layout_itr = pimpl->set_layouts_by_index.find(set_index);
            if (layout_itr != pimpl->set_layouts_by_index.end()) {
                const std::span<const ResolvedBinding> entries(bindings.data() + group_begin, group_end - group_begin);
                const DescriptorSetHandle set =
                    pimpl->device_context->GetDescriptorArena().Acquire(layout_itr->second, 0, false, false, entries);
                encoder.BindDescriptorSet(set_index, set);
            }
            group_begin = group_end;
        }
        if (push != nullptr && push_size != 0) {
            encoder.SetKernelParams(std::span{static_cast<const std::byte *>(push), static_cast<size_t>(push_size)});
        }
        // The compute scope stays open: the next dispatch of this or another kernel records into
        // the same pass, and whatever cannot be recorded inside it closes it first.
        encoder.DispatchCompute(group_count_x, group_count_y, group_count_z);
    }
} // namespace Engine::Rhi
