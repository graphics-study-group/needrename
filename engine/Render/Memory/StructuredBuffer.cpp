#include "StructuredBuffer.h"

namespace Engine {
    struct StructuredBuffer::impl : private std::unordered_map<std::string, StructuredBuffer::VariableEntry> {
        friend class StructuredBuffer;
    };
    StructuredBuffer::StructuredBuffer() : pimpl(std::make_unique<impl>()) {
    }
    StructuredBuffer::~StructuredBuffer() noexcept = default;

    void StructuredBuffer::SetVariable(
        const std::string &name, const void *ptr, size_t size, const std::type_info &type
    ) noexcept {
        pimpl->operator[](name) = VariableEntry{};

        std::vector <std::byte> buf;
        buf.resize(size);
        std::memcpy(buf.data(), ptr, size);

        VariableEntry new_entry{
            .type = &type,
            .size = size
        };
        new_entry.value = std::move(buf);
        pimpl->operator[](name) = std::move(new_entry);
    }
    void StructuredBuffer::SetStructuredBuffer(const std::string &name, const StructuredBuffer &buffer) {
        auto ptr = &buffer;
        this->SetVariable(
            name,
            &ptr,
            sizeof(ptr),
            typeid(StructuredBuffer)
        );
    }
    const StructuredBuffer::VariableEntry *StructuredBuffer::GetVariable(const std::string &name) const {
        auto itr = pimpl->find(name);
        if (itr != pimpl->end()) {
            return &itr->second;
        }
        return nullptr;
    }
} // namespace Engine
