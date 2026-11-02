#include "Rhi/Device/AllocatorState.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Texture/ImageUtils.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/Submission/EpochTracker.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        /**
         * @brief Turn the seam's buffer usage into the standard interface's flags.
         *
         * The two vocabularies are close enough that this is a table, not a
         * translation: the seam names what a buffer is *for*, and the standard
         * interface names what operations are legal on it.
         *
         * A host-visible buffer is the one case that needs more than a flag. The
         * standard interface has no coherent mapping that is written without being
         * mapped, so `HostRandomAccess` and `HostSequentialAccess` add both map
         * flags and the caller creates the buffer mapped at creation.
         */
        bool HostVisible(BufferType type) {
            return type.Test(BufferTypeBits::HostRandomAccess) || type.Test(BufferTypeBits::HostSequentialAccess);
        }

        WGPUBufferUsage ToWgpuUsage(BufferType type) {
            WGPUBufferUsage usage = WGPUBufferUsage_None;
            if (type.Test(BufferTypeBits::CopyFrom)) {
                usage |= WGPUBufferUsage_CopySrc;
            }
            if (type.Test(BufferTypeBits::CopyTo)) {
                usage |= WGPUBufferUsage_CopyDst;
            }
            if (type.Test(BufferTypeBits::ShaderReadOnly)) {
                usage |= WGPUBufferUsage_Uniform;
            }
            if (type.Test(BufferTypeBits::ShaderWrite)) {
                usage |= WGPUBufferUsage_Storage;
            }
            if (type.Test(BufferTypeBits::Index)) {
                usage |= WGPUBufferUsage_Index;
            }
            if (type.Test(BufferTypeBits::Vertex)) {
                usage |= WGPUBufferUsage_Vertex;
            }
            if (type.Test(BufferTypeBits::IndirectDrawCommand)) {
                usage |= WGPUBufferUsage_Indirect;
            }
            if (!HostVisible(type)) {
                return usage;
            }
            // A host-visible buffer has to choose one of two shapes, because the interface
            // allows `MapWrite` only with `CopySrc` and `MapRead` only with `CopyDst`:
            //
            // - a readback target is written by the device and read by the host, so it is
            //   `MapRead | CopyDst` and is mapped when the caller first asks for its address;
            // - anything else is written by the host, so it is `MapWrite | CopySrc` where it is
            //   not also bound in a shader, and a plain `CopyDst` buffer written through the
            //   queue where it is.
            constexpr WGPUBufferUsage k_bound_in_shader = WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage
                                                         | WGPUBufferUsage_Index | WGPUBufferUsage_Vertex
                                                         | WGPUBufferUsage_Indirect;
            if ((usage & k_bound_in_shader) == 0) {
                if (type.Test(BufferTypeBits::CopyTo)) {
                    return (usage & ~WGPUBufferUsage_CopySrc) | WGPUBufferUsage_CopyDst
                           | WGPUBufferUsage_MapRead;
                }
                return (usage | WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc)
                       & ~WGPUBufferUsage_CopyDst;
            }
            usage |= WGPUBufferUsage_CopyDst;
            return usage;
        }

        /// @brief Whether the buffer is created mapped, which requires a MapWrite usage.
        bool MapsAtCreation(BufferType type) {
            if (!HostVisible(type)) {
                return false;
            }
            const WGPUBufferUsage usage = ToWgpuUsage(type);
            return (usage & WGPUBufferUsage_MapWrite) != 0;
        }
    } // namespace

    struct AllocatorState::impl {
        WGPUDevice device{nullptr};
        EpochTracker *retire_sink{nullptr};
    };

    AllocatorState::AllocatorState(DeviceInterface &device_interface) :
        m_device_interface(&device_interface), pimpl(std::make_unique<impl>()) {
        pimpl->device = ToWgpuDevice(device_interface.GetDevice());
    }

    AllocatorState::AllocatorState(AllocatorState &&) noexcept = default;
    AllocatorState::~AllocatorState() = default;

    void AllocatorState::SetRetireSink(EpochTracker *sink) noexcept {
        pimpl->retire_sink = sink;
    }

    BufferAllocation AllocatorState::AllocateBuffer(
        BufferType type, size_t size, const std::string &name
    ) const {
        WGPUBufferDescriptor desc{};
        desc.size = size;
        desc.usage = ToWgpuUsage(type);
        // A host-writable buffer is mapped from the moment it exists. There is no
        // synchronous map afterwards, and the mapped-window contract wants one
        // address for the buffer's whole life anyway.
        desc.mappedAtCreation = MapsAtCreation(type) ? WGPU_TRUE : WGPU_FALSE;
        desc.label = WGPUStringView{name.data(), name.size()};

        WGPUBuffer buffer = wgpuDeviceCreateBuffer(pimpl->device, &desc);
        if (buffer == nullptr) {
            // The standard interface reports failures through the uncaptured-error
            // callback rather than a status code; a null buffer here means it did.
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU: buffer creation failed (%zu bytes, usage 0x%x).",
                size,
                static_cast<unsigned>(desc.usage)
            );
        }

        return BufferAllocation(
            FromWgpuBuffer(buffer), buffer, pimpl->device, type, pimpl->retire_sink
        );
    }

    std::unique_ptr<BufferAllocation> AllocatorState::AllocateBufferUnique(
        BufferType type, size_t size, const std::string &name
    ) const noexcept {
        // A unique allocation is built from the value form; the move is what makes
        // it unique.
        return std::make_unique<BufferAllocation>(AllocateBuffer(type, size, name));
    }

    size_t AllocatorState::GetLiveAllocationCount() const noexcept {
        // No allocation ledger on this backend: a buffer is one object with no
        // separate device memory to account for. Reporting zero rather than a
        // guess keeps a caller from drawing conclusions from a made-up number.
        return 0;
    }
} // namespace Engine::Rhi

namespace Engine::Rhi {
    namespace {
        /**
         * @brief Turn the seam's image usage into the standard interface's flags.
         *
         * The names are close: a framebuffer colour attachment is RENDER_ATTACHMENT
         * here, a blit source is COPY_SRC, and a storage image is STORAGE_BINDING.
         * The seam splits attachment usage into colour and depth-stencil; the
         * standard interface has one flag for both.
         */
        WGPUTextureUsage ToWgpuTextureUsage(ImageMemoryType type) {
            WGPUTextureUsage usage = WGPUTextureUsage_None;
            if (type.Test(ImageMemoryTypeBits::CopyFrom)) {
                usage |= WGPUTextureUsage_CopySrc;
            }
            if (type.Test(ImageMemoryTypeBits::CopyTo)) {
                usage |= WGPUTextureUsage_CopyDst;
            }
            if (type.Test(ImageMemoryTypeBits::ShaderSampled)) {
                usage |= WGPUTextureUsage_TextureBinding;
            }
            if (type.Test(ImageMemoryTypeBits::ShaderRandomAccess)) {
                usage |= WGPUTextureUsage_StorageBinding;
            }
            if (type.Test(ImageMemoryTypeBits::ColorAttachment) || type.Test(ImageMemoryTypeBits::DepthStencilAttachment)) {
                usage |= WGPUTextureUsage_RenderAttachment;
            }
            return usage;
        }
    } // namespace

    ImageAllocation AllocatorState::AllocateImage(const ImageAllocationDescription &desc, const std::string &name) const {
        WGPUTextureDescriptor texture_desc{};
        texture_desc.usage = ToWgpuTextureUsage(desc.type);
        texture_desc.dimension = desc.dimension == ImageType::D1   ? WGPUTextureDimension_1D
                                 : desc.dimension == ImageType::D2 ? WGPUTextureDimension_2D
                                                                   : WGPUTextureDimension_3D;
        // A 3D image counts slices in depth; a 2D image counts them in array layers. The seam
        // keeps the two apart, and the descriptor has one field for both.
        const uint32_t layers_or_depth = desc.dimension == ImageType::D3 ? desc.extent.depth : desc.array_layers;
        texture_desc.size = WGPUExtent3D{desc.extent.width, desc.extent.height, layers_or_depth};
        texture_desc.format = ToWgpuTextureFormat(desc.format);
        texture_desc.mipLevelCount = desc.miplevel;
        texture_desc.sampleCount = static_cast<uint32_t>(desc.samples == SampleCountBits::C1 ? 1 : 4);
        if (desc.is_cube_map) {
            texture_desc.viewFormatCount = 0;
            texture_desc.viewFormats = nullptr;
        }
        texture_desc.label = WGPUStringView{name.data(), name.size()};

        WGPUTexture texture = wgpuDeviceCreateTexture(pimpl->device, &texture_desc);
        if (texture == nullptr) {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU: texture creation failed (%ux%ux%u, format %d).",
                desc.extent.width,
                desc.extent.height,
                desc.extent.depth,
                static_cast<int>(desc.format)
            );
        }
        return ImageAllocation(FromWgpuTexture(texture), texture, pimpl->device, desc.type);
    }

    std::unique_ptr<ImageAllocation> AllocatorState::AllocateImageUnique(
        const ImageAllocationDescription &desc, const std::string &name
    ) const noexcept {
        return std::make_unique<ImageAllocation>(AllocateImage(desc, name));
    }
} // namespace Engine::Rhi
namespace Engine::Rhi {
    bool AllocatorState::QueryFormatFeatures(ImageFormat format, FormatFeature feature) const noexcept {
        // There is no per-format feature query in the standard interface: a format either exists
        // for the usage asked of it or the device rejects the texture at creation. The two answers
        // a caller acts on here are whether the format can be sampled and whether it can be
        // written, and both follow from the format list this backend maps.
        switch (feature) {
        case FormatFeature::VertexBuffer:
            // Any mapped format can feed vertex attributes; the pipeline describes how.
            return ToWgpuTextureFormat(format) != WGPUTextureFormat_Undefined;
        case FormatFeature::StorageImage:
            // Only formats the standard interface lists as storage-capable can be written from a
            // shader; this backend has no storage-texture format configured yet.
            return false;
        case FormatFeature::StorageImageAtomic:
            return false;
        }
        return false;
    }
} // namespace Engine::Rhi

