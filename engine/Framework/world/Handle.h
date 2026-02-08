#ifndef ENGINE_FRAMEWORK_WORLD_HANDLE_INCLUDED
#define ENGINE_FRAMEWORK_WORLD_HANDLE_INCLUDED

#include <cstdint>
#include <functional>

namespace Engine {
    namespace Serialization {
        class Archive;
    }
    class Scene;
    class Component;
    class GameObject;

    namespace detail {
        class HandleBase {
        public:
            HandleBase() = default;
            virtual ~HandleBase() = default;

            virtual uint32_t GetSceneID() const noexcept;
            virtual uint32_t GetID() const noexcept;
            virtual bool IsValid() const noexcept;
            virtual void Reset() noexcept;
            bool operator==(const HandleBase &other) const noexcept;

            // TODO: temporary solution. This should not be serialized automatically.
            void save_to_archive(Engine::Serialization::Archive &archive) const;
            void load_from_archive(Engine::Serialization::Archive &archive);

        protected:
            uint32_t m_sceneID{};
            uint32_t m_ID{};

            HandleBase(uint32_t sceneID, uint32_t ID);
        };
    } // namespace detail

    class ObjectHandle : public detail::HandleBase {
    public:
        ObjectHandle() = default;
        GameObject *GetGameObject() const;
    protected:
        friend class Scene;
        ObjectHandle(uint32_t sceneID, uint32_t ID);
    };

    class ComponentHandle : public detail::HandleBase {
    public:
        ComponentHandle() = default;
        Component *GetComponent() const;
    protected:
        friend class Scene;
        ComponentHandle(uint32_t sceneID, uint32_t ID);
    };
} // namespace Engine

namespace std {
    template <>
    struct hash<Engine::ObjectHandle> {
        size_t operator()(const Engine::ObjectHandle &p) const noexcept {
            return std::hash<uint64_t>()((uint64_t)p.GetSceneID() << 32 | p.GetID());
        }
    };
    template <>
    struct hash<Engine::ComponentHandle> {
        size_t operator()(const Engine::ComponentHandle &p) const noexcept {
            return std::hash<uint64_t>()((uint64_t)p.GetSceneID() << 32 | p.GetID());
        }
    };
} // namespace std

#endif // ENGINE_FRAMEWORK_WORLD_HANDLE_INCLUDED
