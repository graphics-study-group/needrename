#ifndef FRAMEWORK_WORLD_SCENE_INCLUDED
#define FRAMEWORK_WORLD_SCENE_INCLUDED

#include "Handle.h"
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

namespace Engine {
    class LevelAsset;
    class GameObjectAsset;
    class Camera;
    class GameObject;
    class Component;
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

    protected:
        std::vector<std::unique_ptr<GameObject>> m_go_add_queue{};
        std::vector<ObjectHandle> m_go_remove_queue{};
        std::vector<std::unique_ptr<Component>> m_comp_add_queue{};
        std::vector<ComponentHandle> m_comp_remove_queue{};

        std::vector<std::unique_ptr<GameObject>> m_game_objects{};
        std::vector<std::unique_ptr<Component>> m_components{};

        std::unordered_map<ObjectHandle, GameObject *> m_go_map{};
        std::unordered_map<ComponentHandle, Component *> m_comp_map{};

    protected:
        Component &AddComponent(ObjectHandle objectHandle, Component *ptr);
    };
} // namespace Engine

#endif // FRAMEWORK_WORLD_SCENE_INCLUDED
