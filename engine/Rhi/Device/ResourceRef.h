#ifndef ENGINE_RHI_RESOURCEREF_INCLUDED
#define ENGINE_RHI_RESOURCEREF_INCLUDED

#include "Rhi/rhi_export.h"

#include <cstdint>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    class DeviceBuffer;
    class ImmutableResourceCache;
    class Texture;

    /**
     * @brief Never-reused identity of one descriptor-relevant Vulkan object.
     *
     * Identities are issued when the Vulkan object is created and are never
     * issued again after that object is destroyed, so an identity always names
     * exactly one live object and a destroyed object's identity names nothing.
     * This is what lets a cache distinguish two resources whose driver-assigned
     * handle values happen to be equal.
     *
     * The default value is the invalid identity: it names no object.
     */
    class ObjectId {
    public:
        constexpr ObjectId() noexcept = default;
        explicit constexpr ObjectId(uint64_t value) noexcept : m_value(value) {
        }

        /// @brief Get the underlying value, for hashing and debug output.
        constexpr uint64_t GetValue() const noexcept {
            return m_value;
        }
        /// @brief Whether this identity names an object.
        constexpr bool IsValid() const noexcept {
            return m_value != 0;
        }

        friend constexpr bool operator==(const ObjectId &lhs, const ObjectId &rhs) noexcept {
            return lhs.m_value == rhs.m_value;
        }
        friend constexpr bool operator!=(const ObjectId &lhs, const ObjectId &rhs) noexcept {
            return !(lhs == rhs);
        }

    private:
        uint64_t m_value{0};
    };

    /**
     * @brief Issue a fresh identity for one Vulkan object.
     *
     * Identities are process-wide and monotonic, so a value is never reused.
     *
     * @return The new identity, never the invalid identity.
     */
    RHI_API ObjectId MintObjectId() noexcept;

    /**
     * @brief A descriptor-relevant Vulkan object: its handle and its identity.
     *
     * Only the RHI wrapper that owns the Vulkan object can mint a reference, so
     * a bare handle has no path into a binding identity. Equality is identity
     * equality: the handle is carried only to write descriptors and record
     * commands, and MUST NOT be used to compare two resources.
     *
     * @note The default value names no object.
     */
    template <typename Handle>
    struct ResourceRef {
        /// @brief The Vulkan handle, for writing descriptors and recording.
        Handle handle{};
        /// @brief The object's never-reused identity.
        ObjectId id{};

        ResourceRef() noexcept = default;

        bool operator==(const ResourceRef &other) const noexcept {
            return id == other.id;
        }
        bool operator!=(const ResourceRef &other) const noexcept {
            return !(*this == other);
        }

    private:
        constexpr ResourceRef(Handle object_handle, ObjectId object_id) noexcept : handle(object_handle), id(object_id) {
        }

        friend class DeviceBuffer;
        friend class ImmutableResourceCache;
        friend class Texture;
    };

    /// @brief A buffer allocation's reference.
    using BufferRef = ResourceRef<vk::Buffer>;
    /// @brief An image view's reference.
    using ViewRef = ResourceRef<vk::ImageView>;
    /// @brief A sampler's reference.
    using SamplerRef = ResourceRef<vk::Sampler>;
    /// @brief A descriptor-set layout's reference.
    using DescriptorSetLayoutRef = ResourceRef<vk::DescriptorSetLayout>;
} // namespace Engine::Rhi

#endif // ENGINE_RHI_RESOURCEREF_INCLUDED
