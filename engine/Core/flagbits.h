#ifndef CORE_FLAGBITS
#define CORE_FLAGBITS

#include <type_traits>
#include <initializer_list>

namespace Engine {
    template<class T>
    concept IsEnum = std::is_enum<T>::value;
    /**
     * @brief Type-safe bit flag to be used with scoped or unscoped enums.
     * Maybe we can directly use vk::Flags and avoid re-inventing the wheels...
     */
    template <class T> requires IsEnum<T>
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
        constexpr explicit operator UnderlyingType () {
            return m_flags;
        }

        constexpr operator bool() const {
            return m_flags != static_cast<UnderlyingType>(0);
        }

        friend constexpr Flags operator|(Flags lhs, T rhs) {
            return Flags(lhs.m_flags | static_cast<UnderlyingType>(rhs));
        }
        friend constexpr Flags operator|(Flags lhs, Flags rhs) {
            return Flags(lhs.m_flags | rhs.m_flags);
        }
        friend constexpr Flags operator&(Flags lhs, T rhs) {
            return Flags(lhs.m_flags & static_cast<UnderlyingType>(rhs));
        }
        friend constexpr Flags operator&(Flags lhs, Flags rhs) {
            return Flags(lhs.m_flags & rhs.m_flags);
        }
        friend constexpr Flags operator^(Flags lhs, T rhs) {
            return Flags(lhs.m_flags ^ static_cast<UnderlyingType>(rhs));
        }
        friend constexpr Flags operator^(Flags lhs, Flags rhs) {
            return Flags(lhs.m_flags ^ rhs.m_flags);
        }

        friend constexpr Flags& operator|=(Flags& lhs, T rhs) {
            lhs.m_flags |= static_cast<UnderlyingType>(rhs);
            return lhs;
        }
        friend constexpr Flags& operator|=(Flags& lhs, Flags rhs) {
            lhs.m_flags |= rhs.m_flags;
            return lhs;
        }
        friend constexpr Flags& operator&=(Flags& lhs, T rhs) {
            lhs.m_flags &= static_cast<UnderlyingType>(rhs);
            return lhs;
        }
        friend constexpr Flags& operator&=(Flags& lhs, Flags rhs) {
            lhs.m_flags &= rhs.m_flags;
            return lhs;
        }
        friend constexpr Flags& operator^=(Flags& lhs, T rhs) {
            lhs.m_flags ^= static_cast<UnderlyingType>(rhs);
            return lhs;
        }
        friend constexpr Flags& operator^=(Flags& lhs, Flags rhs) {
            lhs.m_flags ^= rhs.m_flags;
            return lhs;
        }

        friend constexpr Flags operator~(const Flags& bf) {
            return Flags(~bf.m_flags);
        }

        friend constexpr bool operator==(const Flags& lhs, const Flags& rhs) {
            return lhs.m_flags == rhs.m_flags;
        }
        friend constexpr bool operator!=(const Flags& lhs, const Flags& rhs) {
            return lhs.m_flags != rhs.m_flags;
        }

    };
}

#endif // CORE_FLAGBITS
