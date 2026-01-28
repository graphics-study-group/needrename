#ifndef FRAMEWORK_WORLD_SCENE_INCLUDED
#define FRAMEWORK_WORLD_SCENE_INCLUDED

#include "Handle.h"
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

namespace Engine {
    class GameObject;
    class EventQueue;
    class Component;
    class SceneAsset;
    namespace Reflection {
        class Type;
    }

    class Scene {
    public:
        Scene();
        ~Scene();

        void AddInitEvent();
        void AddTickEvent();

        GameObject &CreateGameObject();
        Component &CreateComponent(GameObject &parent, const Reflection::Type &type);
        template <typename T>
        T &CreateComponent(GameObject &parent) {
            static_assert(std::is_base_of<Component, T>::value, "T must be derived from Component");
            return static_cast<T &>(AddComponent(parent.GetHandle(), new T(&parent)));
        }

        void RemoveGameObject(ObjectHandle handle);
        void RemoveComponent(ComponentHandle handle);

        /// @brief Load or remove all GameObjects or Components in the command queue.
        void FlushCmdQueue();
        /// @brief Process all events in the event queue.
        void ProcessEvents();
        /// @brief Clear all events in the event queue.
        void ClearEventQueue();

        GameObject *GetGameObject(ObjectHandle handle);
        Component *GetComponent(ComponentHandle handle);
        template <typename T>
        T *GetComponent(ComponentHandle handle) {
            return dynamic_cast<T *>(GetComponent(handle));
        }
        GameObject &GetGameObjectRef(ObjectHandle handle);
        Component &GetComponentRef(ComponentHandle handle);
        template <typename T>
        T &GetComponentRef(ComponentHandle handle) {
            return dynamic_cast<T &>(GetComponentRef(handle));
        }

        const std::vector<std::unique_ptr<GameObject>> &GetGameObjects() const;
        const std::vector<std::unique_ptr<Component>> &GetComponents() const;

        Scene(const Scene &) = delete;
        Scene &operator=(const Scene &) = delete;
        Scene(Scene &&) = delete;
        Scene &operator=(Scene &&) = delete;

        /// @brief Clear all GameObjects and Components in the scene.
        void Clear();

        /// @brief Add a SceneAsset to the scene.
        /// @param asset 
        void AddSceneAsset(SceneAsset &asset);

    protected:
        friend class SceneAsset;

        std::vector<std::unique_ptr<GameObject>> m_go_add_queue{};
        std::vector<ObjectHandle> m_go_remove_queue{};
        std::vector<std::unique_ptr<Component>> m_comp_add_queue{};
        std::vector<ComponentHandle> m_comp_remove_queue{};

        std::vector<std::unique_ptr<GameObject>> m_game_objects{};
        std::vector<std::unique_ptr<Component>> m_components{};

        std::unordered_map<ObjectHandle, GameObject *> m_go_map{};
        std::unordered_map<ComponentHandle, Component *> m_comp_map{};

        std::unique_ptr<EventQueue> m_event_queue{};

    protected:
        Component &AddComponent(ObjectHandle objectHandle, Component *ptr);
    };
} // namespace Engine

#endif // FRAMEWORK_WORLD_SCENE_INCLUDED
