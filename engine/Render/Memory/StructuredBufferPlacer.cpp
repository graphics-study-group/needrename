#include "StructuredBufferPlacer.h"

#include "StructuredBuffer.h"

#include <any>
#include <unordered_map>
#include <SDL3/SDL.h>

#ifndef NDEBUG
#include <cxxabi.h>
namespace {
    std::string DemangleTypeName(const char * p) {
        char * demangled_name_buf = abi::__cxa_demangle(p, nullptr, nullptr, nullptr);
        std::string name{demangled_name_buf};
        free(demangled_name_buf);
        return name;
    }
}
#else
namespace {
    std::string DemangleTypeName(const char * p) {
        return std::string{p};
    }
}
#endif

namespace Engine {
    struct StructuredBufferPlacer::impl {
        struct TypeInfo {
            // type info is used for type checking only.
            const std::type_info * info {nullptr};
            size_t offset {};
            // Size of the variable. If the variable is a buffer,
            // then is the maximal size of the buffer.
            size_t size {};

            const StructuredBufferPlacer * subbuffer{nullptr};
        };

        // TODO: replace `std::string` by a index and a string table for better performance.
        std::unordered_map <std::string, TypeInfo> mapping {};

        struct SizeStatistic{
            bool max_size_dirty{0};
            size_t max_size{0};
        };
        mutable SizeStatistic size;
    };

    StructuredBufferPlacer::StructuredBufferPlacer() noexcept : pimpl(std::make_unique<impl>()) {
    }

    StructuredBufferPlacer::~StructuredBufferPlacer() noexcept = default;

    void StructuredBufferPlacer::AddVariable(
        const std::string &name,
        size_t offset,
        size_t size,
        const std::type_info *type
    ) {
        pimpl->size.max_size_dirty = true;

        pimpl->mapping[name] = impl::TypeInfo{
            .info = type,
            .offset = offset,
            .size = size,
            .subbuffer = nullptr
        };
    }

    void StructuredBufferPlacer::AddStructuredBuffer(
        const std::string &name,
        size_t offset,
        const StructuredBufferPlacer & buffer
    ) {
        pimpl->size.max_size_dirty = true;

        pimpl->mapping[name] = impl::TypeInfo{
            .info = &typeid(StructuredBuffer),
            .offset = offset,
            .size = 0,
            .subbuffer = &buffer
        };
    }

    size_t StructuredBufferPlacer::CalculateMaxSize() const noexcept {
        if (!pimpl->size.max_size_dirty)    return pimpl->size.max_size;

        size_t max_size {0};
        for (const auto & [k, v] : pimpl->mapping) {
            size_t element_size{0};
            if (v.info == &typeid(StructuredBuffer)) {
                assert(v.subbuffer);
                element_size = v.subbuffer->CalculateMaxSize();
            } else {
                element_size = v.size;
            }
            max_size = std::max(max_size, element_size + v.offset);
        }
        pimpl->size.max_size_dirty = false;
        pimpl->size.max_size = max_size;
        return max_size;
    }

    void StructuredBufferPlacer::WriteBuffer(const StructuredBuffer &data, std::byte *buffer) const noexcept {
        for (const auto & [name, info] : pimpl->mapping) {
            auto varptr = data.GetVariable(name);
            if (varptr == nullptr) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Cannot find variable of name %s in the given structured buffer.",
                    name.c_str()
                );
                continue;
            }
            if (info.info != nullptr && info.info != varptr->type) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Variable of name %s has type %s in the placer but %s in the buffer.",
                    name.c_str(),
                    DemangleTypeName(info.info->name()).c_str(),
                    DemangleTypeName(varptr->type->name()).c_str()
                );
            }

            if (info.info == &typeid(StructuredBuffer)) {
                // Recursively process buffer writes.
                using cbp = const StructuredBuffer *;
                assert(info.subbuffer != nullptr);
                assert(varptr->value.size() >= sizeof(cbp));
                auto embedded_buffer_ptr = *reinterpret_cast<const cbp *>(varptr->value.data());
                info.subbuffer->WriteBuffer(*embedded_buffer_ptr, buffer + info.offset);
            } else {
                // For other standard-layout variables, just memcpy them.
                assert(varptr->value.size() >= info.size);
                std::memcpy(
                    buffer + info.offset,
                    varptr->value.data(),
                    info.size
                );
            }
        }
    }
    void StructuredBufferPlacer::WriteBuffer(const StructuredBuffer &data, std::vector<std::byte> &buffer) const {
        buffer.resize(CalculateMaxSize());
        WriteBuffer(data, buffer.data());
    }
} // namespace Engine
