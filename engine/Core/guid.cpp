/*!
    \file uuid.cpp
    \brief Universally unique identifier (GUID) implementation

    Original Author: Ivan Shynkarenka
    Original Date: 18.08.2016

    Modified by: Captain Harry Chen
    Modified date: 24.03.2026

    \copyright MIT License
*/

#include "guid.h"

#if defined(__EMSCRIPTEN__)
#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#elif defined(__MSYS__) || defined(_WIN32) || defined(_WIN64)
#include <rpc.h>
#include <windows.h>
#elif defined(unix) || defined(__unix) || defined(__unix__) || defined(__APPLE__)
#include <uuid/uuid.h>
#endif

#if defined(__EMSCRIPTEN__)
namespace {
    /// @brief Stamp the version and variant bits a v4 UUID carries.
    void StampVersion4(std::array<uint8_t, 16> &data) {
        data[6] = static_cast<uint8_t>((data[6] & 0x0F) | 0x40);
        data[8] = static_cast<uint8_t>((data[8] & 0x3F) | 0x80);
    }

    /// @brief Fill a UUID from a seeded generator.
    void FillRandomGuid(std::array<uint8_t, 16> &data) {
        static std::mt19937_64 engine{std::random_device{}()};
        std::uniform_int_distribution<uint32_t> byte{0, 255};
        for (uint8_t &value : data) value = static_cast<uint8_t>(byte(engine));
        StampVersion4(data);
    }

    /// @brief Fill a UUID that keeps ascending across calls.
    ///
    /// A browser has no system UUID service, so the value is built from the
    /// monotonic clock and a process-wide counter: the leading bytes carry both,
    /// which keeps successive calls ordered and distinct.
    void FillSequentialGuid(std::array<uint8_t, 16> &data) {
        static std::atomic<uint64_t> counter{0};
        const uint64_t now = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const uint64_t sequence = counter.fetch_add(1, std::memory_order_relaxed);
        FillRandomGuid(data);
        std::memcpy(data.data(), &now, 6);
        std::memcpy(data.data() + 8, &sequence, sizeof(sequence));
        StampVersion4(data);
    }
} // namespace
#endif

namespace Engine {

    std::string GUID::string() const {
        const char *digits = "0123456789abcdef";

        std::string result(36, '0');

        int index = 0;
        for (auto value : _data) {
            result[index++] = digits[(value >> 4) & 0x0F];
            result[index++] = digits[(value >> 0) & 0x0F];
            if ((index == 8) || (index == 13) || (index == 18) || (index == 23)) result[index++] = '-';
        }

        return result;
    }

    GUID GUID::Sequential() {
        GUID result;
#if defined(__MSYS__) || defined(_WIN32) || defined(_WIN64)
        ::GUID uuid;
        if (UuidCreateSequential(&uuid) != RPC_S_OK) throw std::runtime_error("Cannot generate sequential GUID!");

        result._data[0] = (uuid.Data1 >> 24) & 0xFF;
        result._data[1] = (uuid.Data1 >> 16) & 0xFF;
        result._data[2] = (uuid.Data1 >> 8) & 0xFF;
        result._data[3] = (uuid.Data1 >> 0) & 0xFF;

        result._data[4] = (uuid.Data2 >> 8) & 0xFF;
        result._data[5] = (uuid.Data2 >> 0) & 0xFF;

        result._data[6] = (uuid.Data3 >> 8) & 0xFF;
        result._data[7] = (uuid.Data3 >> 0) & 0xFF;

        result._data[8] = uuid.Data4[0];
        result._data[9] = uuid.Data4[1];

        result._data[10] = uuid.Data4[2];
        result._data[11] = uuid.Data4[3];
        result._data[12] = uuid.Data4[4];
        result._data[13] = uuid.Data4[5];
        result._data[14] = uuid.Data4[6];
        result._data[15] = uuid.Data4[7];
#elif defined(__EMSCRIPTEN__)
        FillSequentialGuid(result._data);
#elif defined(unix) || defined(__unix) || defined(__unix__) || defined(__APPLE__)
        uuid_t uuid;
        uuid_generate_time(uuid);

        result._data[0] = uuid[0];
        result._data[1] = uuid[1];
        result._data[2] = uuid[2];
        result._data[3] = uuid[3];

        result._data[4] = uuid[4];
        result._data[5] = uuid[5];

        result._data[6] = uuid[6];
        result._data[7] = uuid[7];

        result._data[8] = uuid[8];
        result._data[9] = uuid[9];

        result._data[10] = uuid[10];
        result._data[11] = uuid[11];
        result._data[12] = uuid[12];
        result._data[13] = uuid[13];
        result._data[14] = uuid[14];
        result._data[15] = uuid[15];
#endif
        return result;
    }

    GUID GUID::Random() {
        GUID result;
#if defined(__MSYS__) || defined(_WIN32) || defined(_WIN64)
        ::GUID uuid;
        if (UuidCreate(&uuid) != RPC_S_OK) throw std::runtime_error("Cannot generate random GUID!");

        result._data[0] = (uuid.Data1 >> 24) & 0xFF;
        result._data[1] = (uuid.Data1 >> 16) & 0xFF;
        result._data[2] = (uuid.Data1 >> 8) & 0xFF;
        result._data[3] = (uuid.Data1 >> 0) & 0xFF;

        result._data[4] = (uuid.Data2 >> 8) & 0xFF;
        result._data[5] = (uuid.Data2 >> 0) & 0xFF;

        result._data[6] = (uuid.Data3 >> 8) & 0xFF;
        result._data[7] = (uuid.Data3 >> 0) & 0xFF;

        result._data[8] = uuid.Data4[0];
        result._data[9] = uuid.Data4[1];

        result._data[10] = uuid.Data4[2];
        result._data[11] = uuid.Data4[3];
        result._data[12] = uuid.Data4[4];
        result._data[13] = uuid.Data4[5];
        result._data[14] = uuid.Data4[6];
        result._data[15] = uuid.Data4[7];
#elif defined(__EMSCRIPTEN__)
        FillRandomGuid(result._data);
#elif defined(unix) || defined(__unix) || defined(__unix__) || defined(__APPLE__)
        uuid_t uuid;
        uuid_generate_random(uuid);

        result._data[0] = uuid[0];
        result._data[1] = uuid[1];
        result._data[2] = uuid[2];
        result._data[3] = uuid[3];

        result._data[4] = uuid[4];
        result._data[5] = uuid[5];

        result._data[6] = uuid[6];
        result._data[7] = uuid[7];

        result._data[8] = uuid[8];
        result._data[9] = uuid[9];

        result._data[10] = uuid[10];
        result._data[11] = uuid[11];
        result._data[12] = uuid[12];
        result._data[13] = uuid[13];
        result._data[14] = uuid[14];
        result._data[15] = uuid[15];
#endif
        return result;
    }
} // namespace Engine
