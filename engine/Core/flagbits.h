#ifndef CORE_FLAGBITS
#define CORE_FLAGBITS

#include <initializer_list>
#include <type_traits>

namespace Engine {
    /**
     * @brief Type-safe bit flag to be used with scoped or unscoped enums.
     * Maybe we can directly use vk::Flags and avoid re-inventing the wheels...
     */
    template <class T>
        requires std::is_enum_v<T>
    class Flags {
        using UnderlyingType = std::underlying_type_t<T>;
        UnderlyingType m_flags;

    public:
        /// @brief Default constructor with no bit set.
        constexpr Flags() : m_flags(static_cast<UnderlyingType>(0)) {
        }

        /// @brief Construct a flag with a single bit set.
        constexpr explicit Flags(T bit) : m_flags(static_cast<UnderlyingType>(bit)) {
        }

        /// @brief Construct a flag with multiple bits set.
        constexpr Flags(std::initializer_list<T> bits) : Flags() {
            for (T b : bits) {
                m_flags |= static_cast<UnderlyingType>(b);
            }
        }

        /// @brief Construct a flag from underlying integer.
        constexpr explicit Flags(UnderlyingType bits) : m_flags(bits) {
        }

        /// @brief Convert to the underlying integer type.
        constexpr explicit operator UnderlyingType() const noexcept {
            return m_flags;
        }

        /// @brief Check whether any flag is set.
        constexpr operator bool() const noexcept {
            return m_flags != static_cast<UnderlyingType>(0);
        }

        /// @brief Set a specific bit to be true.
        constexpr void Set(T bit) noexcept {
            m_flags |= static_cast<UnderlyingType>(bit);
        }

        /// @brief Test whether a bit (or any of the given bits) is set.
        constexpr bool Test(T bit) const noexcept {
            return static_cast<bool>(m_flags & static_cast<UnderlyingType>(bit));
        }

        /// @brief Mask the current flag with given bits.
        constexpr void Mask(T bit) noexcept {
            m_flags &= static_cast<UnderlyingType>(bit);
        }

        friend constexpr Flags operator|(Flags lhs, T rhs) noexcept {
            return Flags(lhs.m_flags | static_cast<UnderlyingType>(rhs));
        }
        friend constexpr Flags operator|(Flags lhs, Flags rhs) noexcept {
            return Flags(lhs.m_flags | rhs.m_flags);
        }
        friend constexpr Flags operator&(Flags lhs, T rhs) noexcept {
            return Flags(lhs.m_flags & static_cast<UnderlyingType>(rhs));
        }
        friend constexpr Flags operator&(Flags lhs, Flags rhs) noexcept {
            return Flags(lhs.m_flags & rhs.m_flags);
        }
        friend constexpr Flags operator^(Flags lhs, T rhs) noexcept {
            return Flags(lhs.m_flags ^ static_cast<UnderlyingType>(rhs));
        }
        friend constexpr Flags operator^(Flags lhs, Flags rhs) noexcept {
            return Flags(lhs.m_flags ^ rhs.m_flags);
        }

        friend constexpr Flags &operator|=(Flags &lhs, T rhs) noexcept {
            lhs.m_flags |= static_cast<UnderlyingType>(rhs);
            return lhs;
        }
        friend constexpr Flags &operator|=(Flags &lhs, Flags rhs) noexcept {
            lhs.m_flags |= rhs.m_flags;
            return lhs;
        }
        friend constexpr Flags &operator&=(Flags &lhs, T rhs) noexcept {
            lhs.m_flags &= static_cast<UnderlyingType>(rhs);
            return lhs;
        }
        friend constexpr Flags &operator&=(Flags &lhs, Flags rhs) noexcept {
            lhs.m_flags &= rhs.m_flags;
            return lhs;
        }
        friend constexpr Flags &operator^=(Flags &lhs, T rhs) noexcept {
            lhs.m_flags ^= static_cast<UnderlyingType>(rhs);
            return lhs;
        }
        friend constexpr Flags &operator^=(Flags &lhs, Flags rhs) noexcept {
            lhs.m_flags ^= rhs.m_flags;
            return lhs;
        }

        friend constexpr Flags operator~(const Flags &bf) noexcept {
            return Flags(~bf.m_flags);
        }

        friend constexpr bool operator==(const Flags &lhs, const Flags &rhs) noexcept {
            return lhs.m_flags == rhs.m_flags;
        }
        friend constexpr bool operator!=(const Flags &lhs, const Flags &rhs) noexcept {
            return lhs.m_flags != rhs.m_flags;
        }
    };
} // namespace Engine

#endif // CORE_FLAGBITS
