#ifndef ENGINE_FRAMEWORK_WORLD_HANDLE_INCLUDED
#define ENGINE_FRAMEWORK_WORLD_HANDLE_INCLUDED

#include <Core/guid.h>
#include <Reflection/macros.h>
#include <cstdint>
#include <functional>

namespace Engine {
    class WorldSystem;

    namespace detail {
        class REFL_SER_CLASS(REFL_WHITELIST) HandleBase {
            REFL_SER_BODY(HandleBase)
        public:
            REFL_ENABLE HandleBase() = default;
            REFL_ENABLE HandleBase(GUID data);
            virtual ~HandleBase() = default;

            REFL_ENABLE virtual GUID GetData() const noexcept;
            REFL_ENABLE virtual bool IsValid() const noexcept;
            REFL_ENABLE virtual void Reset() noexcept;
            bool operator==(const HandleBase &other) const noexcept;

        protected:
            friend class Engine::WorldSystem;
            GUID m_data{};
        };
    } // namespace detail

    class REFL_SER_CLASS(REFL_WHITELIST) ObjectHandle : public detail::HandleBase {
        REFL_SER_BODY(ObjectHandle)
    public:
        REFL_ENABLE ObjectHandle() = default;
        REFL_ENABLE ObjectHandle(GUID data);
    };

    class REFL_SER_CLASS(REFL_WHITELIST) ComponentHandle : public detail::HandleBase {
        REFL_SER_BODY(ComponentHandle)
    public:
        REFL_ENABLE ComponentHandle() = default;
        REFL_ENABLE ComponentHandle(GUID data);
    };
} // namespace Engine

namespace std {
    template <>
    struct hash<Engine::ObjectHandle> {
        size_t operator()(const Engine::ObjectHandle &p) const noexcept {
            return std::hash<Engine::GUID>()(p.GetData());
        }
    };
    template <>
    struct hash<Engine::ComponentHandle> {
        size_t operator()(const Engine::ComponentHandle &p) const noexcept {
            return std::hash<Engine::GUID>()(p.GetData());
        }
    };
} // namespace std

#endif // ENGINE_FRAMEWORK_WORLD_HANDLE_INCLUDED
