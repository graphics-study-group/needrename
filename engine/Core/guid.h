/*!
    \file guid.h
    \brief Universally unique identifier (UUID) definition

    Original Author: Ivan Shynkarenka
    Original Date: 18.08.2016

    Modified by: Captain Harry Chen
    Modified date: 24.03.2026

    \copyright MIT License
*/
#ifndef ENGINE_CORE_GUID_INCLUDED
#define ENGINE_CORE_GUID_INCLUDED

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace Engine {

    //! Universally unique identifier (UUID)
    /*!
        A universally unique identifier (UUID) is an identifier standard used
        in software construction. This implementation generates the following
        UUID types:
        - Nil UUID0 (all bits set to zero)
        - Sequential UUID1 (time based version)
        - Random UUID4 (randomly or pseudo-randomly generated version)

        A UUID is simply a 128-bit value: "123e4567-e89b-12d3-a456-426655440000"

        Not thread-safe.

        https://en.wikipedia.org/wiki/Universally_unique_identifier
        https://www.ietf.org/rfc/rfc4122.txt
    */
    class GUID {
    public:
        //! Default constructor
        constexpr GUID() : _data() {
            _data.fill(0);
        }
        //! Initialize UUID with a given string literal
        /*!
            \param uuid - UUID string literal
        */
        template <size_t N>
        explicit constexpr GUID(const char (&uuid)[N]) : GUID(uuid, N) {
        }
        //! Initialize UUID with a given string literal
        /*!
            \param uuid - UUID string literal
            \param size - UUID string literal size
        */
        explicit constexpr GUID(const char *uuid, size_t size);
        //! Initialize UUID with a given string
        /*!
            \param uuid - UUID string
        */
        explicit GUID(const std::string &uuid) : GUID(uuid.data(), uuid.size()) {
        }
        //! Initialize UUID with a given 16 bytes data buffer
        /*!
            \param data - UUID 16 bytes data buffer
        */
        explicit GUID(const std::array<uint8_t, 16> &data) : _data(data) {
        }
        GUID(const GUID &) = default;
        GUID(GUID &&) noexcept = default;
        ~GUID() = default;

        GUID &operator=(const std::string &uuid) {
            _data = GUID(uuid).data();
            return *this;
        }
        GUID &operator=(const std::array<uint8_t, 16> &data) {
            _data = data;
            return *this;
        }
        GUID &operator=(const GUID &) = default;
        GUID &operator=(GUID &&) noexcept = default;

        // UUID comparison
        friend bool operator==(const GUID &uuid1, const GUID &uuid2) {
            return uuid1._data == uuid2._data;
        }
        friend bool operator!=(const GUID &uuid1, const GUID &uuid2) {
            return uuid1._data != uuid2._data;
        }
        friend bool operator<(const GUID &uuid1, const GUID &uuid2) {
            return uuid1._data < uuid2._data;
        }
        friend bool operator>(const GUID &uuid1, const GUID &uuid2) {
            return uuid1._data > uuid2._data;
        }
        friend bool operator<=(const GUID &uuid1, const GUID &uuid2) {
            return uuid1._data <= uuid2._data;
        }
        friend bool operator>=(const GUID &uuid1, const GUID &uuid2) {
            return uuid1._data >= uuid2._data;
        }

        //! Check if the UUID is nil UUID0 (all bits set to zero)
        explicit operator bool() const noexcept {
            return *this != Nil();
        }

        //! Get the UUID data buffer
        std::array<uint8_t, 16> &data() noexcept {
            return _data;
        }
        //! Get the UUID data buffer
        const std::array<uint8_t, 16> &data() const noexcept {
            return _data;
        }

        //! Get string from the current UUID in format "00000000-0000-0000-0000-000000000000"
        std::string string() const;

        //! Generate nil UUID0 (all bits set to zero)
        static GUID Nil() {
            return GUID();
        }
        //! Generate sequential UUID1 (time based version)
        static GUID Sequential();
        //! Generate random UUID4 (randomly or pseudo-randomly generated version)
        static GUID Random();

        //! Output instance into the given output stream
        friend std::ostream &operator<<(std::ostream &os, const GUID &uuid) {
            os << uuid.string();
            return os;
        }

        //! Swap two instances
        void swap(GUID &uuid) noexcept;
        friend void swap(GUID &uuid1, GUID &uuid2) noexcept;

    private:
        std::array<uint8_t, 16> _data;
    };

    /*! \example system_uuid.cpp Universally unique identifier (UUID) example */

} // namespace Engine

//! Initialize UUID with a given string literal
/*!
    \param uuid - UUID string literal
    \param size - UUID string literal size
*/
constexpr Engine::GUID operator""_uuid(const char *uuid, size_t size) {
    return Engine::GUID(uuid, size);
}

#include "guid.inl"

#endif // ENGINE_CORE_GUID_INCLUDED
