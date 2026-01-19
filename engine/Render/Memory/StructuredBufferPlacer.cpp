#include "StructuredBufferPlacer.h"

#include <any>
#include <unordered_map>
#include <SDL3/SDL.h>

namespace Engine {
    class StructuredBuffer : private std::unordered_map <std::string, std::unique_ptr<std::byte[]>> {

        const StructuredBufferPlacer & parent;

        StructuredBuffer(
            const StructuredBufferPlacer & parent
        ) noexcept : std::unordered_map <std::string, std::unique_ptr<std::byte[]>>(), parent(parent) {
        }

        void SetVariable(
            const std::string & name,
            const void * data,
            size_t size
        ) noexcept {
            auto ptr = std::make_unique<std::byte[]>(size);
            std::memcpy(ptr.get(), data, size);
            (*this)[name] = std::move(ptr);
        }

        friend class StructuredBufferPlacer;
        friend class std::unique_ptr <StructuredBuffer>;
    };

    struct StructuredBufferPlacer::impl {
        struct TypeInfo {
            // type info is used for type checking only.
            const std::type_info * info {nullptr};
            size_t offset {};
            // Size of the variable. If the variable is a buffer,
            // then is the maximal size of the buffer.
            size_t size {};
        };

        // TODO: replace `std::string` by a index and a string table for better performance.
        std::unordered_map <std::string, TypeInfo> mapping {};

        size_t maximal_size{0};
    };

    bool StructuredBufferPlacer::TypeCheck(
        const std::string &name,
        const std::type_info &type
    ) const noexcept {
        auto itr = pimpl->mapping.find(name);
        // name not found
        if (itr == pimpl->mapping.end())    return false;
        // Skip type check
        if (itr->second.info == nullptr)    return true;

        return (*itr->second.info == type);
    }

    StructuredBufferPlacer::StructuredBufferPlacer() noexcept : pimpl(std::make_unique<impl>()) {
    }

    StructuredBufferPlacer::~StructuredBufferPlacer() noexcept = default;

    void StructuredBufferPlacer::AddVariable(
        const std::string &name,
        size_t offset,
        size_t size,
        const std::type_info *type
    ) {
        pimpl->mapping[name] = impl::TypeInfo{
            .info = type,
            .offset = offset,
            .size = size
        };

        pimpl->maximal_size = std::max(pimpl->maximal_size, offset + size);
    }

    std::unique_ptr<StructuredBuffer> StructuredBufferPlacer::CreateBuffer() const noexcept {
        return std::unique_ptr<StructuredBuffer>(new StructuredBuffer(*this));
    }
    void StructuredBufferPlacer::SetVariable(
        StructuredBuffer &buf, const std::string &name, const void *ptr
    ) const noexcept {
        auto itr = pimpl->mapping.find(name);
        if (itr == pimpl->mapping.end()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot find variable of name %s.", name.c_str());
            return;
        }

        assert(itr->second.info != &typeid(StructuredBufferPlacer));
        assert(&buf.parent == this);
        buf.SetVariable(name, ptr, itr->second.size);
    }

    void StructuredBufferPlacer::AddStructuredBuffer(
        const std::string &name,
        size_t offset,
        size_t buffer_size
    ) {
        pimpl->mapping[name] = impl::TypeInfo{
            .info = &typeid(StructuredBuffer),
            .offset = offset,
            .size = buffer_size
        };
    }

    void StructuredBufferPlacer::SetStructuredBuffer(
        StructuredBuffer &buf, const std::string &name, const StructuredBuffer &buffer
    ) {
        assert(TypeCheck(name, typeid(StructuredBuffer)));
        assert(&buf.parent == this);
        buf.SetVariable(
            name,
            &buffer,
            sizeof(StructuredBuffer *)
        );
    }

    void StructuredBufferPlacer::WriteBuffer(const StructuredBuffer &data, std::byte *buffer) const noexcept {
        for (const auto & [name, info] : pimpl->mapping) {
            auto var_itr = data.find(name);
            if (var_itr == data.end()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Cannot find variable of name %s in the given structured buffer.",
                    name.c_str()
                );
                continue;
            }

            if (info.info == &typeid(StructuredBuffer)) {
                // Recursively process buffer writes.
                auto embedded_buffer_ptr = reinterpret_cast<const StructuredBuffer *>(var_itr->second.get());
                embedded_buffer_ptr->parent.WriteBuffer(*embedded_buffer_ptr, buffer + info.offset);
            } else {
                // For other standard-layout variables, just memcpy them.
                std::memcpy(
                    buffer + info.offset,
                    var_itr->second.get(),
                    info.size
                );
            }
        }
    }
    void StructuredBufferPlacer::WriteBuffer(const StructuredBuffer &data, std::vector<std::byte> &buffer) const {
        buffer.resize(pimpl->maximal_size);
        WriteBuffer(data, buffer.data());
    }
} // namespace Engine
