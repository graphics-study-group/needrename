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
            HandleBase(uint32_t ID);
            virtual ~HandleBase() = default;

            virtual uint32_t GetSceneID() const noexcept;
            virtual uint32_t GetID() const noexcept;
            virtual bool IsValid() const noexcept;
            virtual void Reset() noexcept;
            bool operator==(const HandleBase &other) const noexcept;

            virtual void save_to_archive(Engine::Serialization::Archive &archive) const;
            virtual void load_from_archive(Engine::Serialization::Archive &) = 0;

        protected:
            uint32_t m_sceneID{};
            uint32_t m_ID{};

            HandleBase(uint32_t sceneID, uint32_t ID);
        };
    } // namespace detail

    class ObjectHandle : public detail::HandleBase {
    public:
        ObjectHandle() = default;
        ObjectHandle(uint32_t ID);

        GameObject *GetGameObject() const;
        virtual void load_from_archive(Engine::Serialization::Archive &archive) override;

    protected:
        friend class Scene;
        ObjectHandle(uint32_t sceneID, uint32_t ID);
    };

    class ComponentHandle : public detail::HandleBase {
    public:
        ComponentHandle() = default;
        ComponentHandle(uint32_t ID);

        Component *GetComponent() const;
        virtual void load_from_archive(Engine::Serialization::Archive &archive) override;

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
