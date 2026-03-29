#ifndef FRAMEWORK_WORLD_SCENE_INCLUDED
#define FRAMEWORK_WORLD_SCENE_INCLUDED

#include "Handle.h"
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

namespace Engine {
    class WorldSystem;
    class GameObject;
    class EventQueue;
    class Component;
    class SceneAsset;
    namespace Reflection {
        class Type;
    }

    /**
     * @brief Scene is a container for GameObjects and Components.
     * The Scene manages the creation, storage, and lookup of all GameObject and Component instances.
     * The Scene contains a event queue, which manages the events of all Components in the scene.
     * Usually only the main scene in WorldSystem will process the events.
     *
     * A Scene can be gotten by its ID from WorldSystem.
     * Creation and deletion operations about GameObjects and Components are queued and
     * processed via Scene::FlushCmdQueue() for safe lifetime management.
     */
    class Scene {
    protected:
        friend class WorldSystem;
        Scene(uint32_t sceneID);

    public:
        virtual ~Scene();

        /**
         * @brief Get the ID of the scene.
         * @return The ID of the scene.
         */
        uint32_t GetID() const noexcept;

        /**
         * @brief Get a null handle for GameObjects of the scene.
         * @return A null handle for GameObjects.
         */
        ObjectHandle GetNullObjectHandle() const noexcept;

        /**
         * @brief Get a null handle for Components of the scene.
         * @return A null handle for Components.
         */
        ComponentHandle GetNullComponentHandle() const noexcept;

        /**
         * @brief Add the init events of all Components in the scene to the scene event queue.
         */
        void AddInitEvent();

        /**
         * @brief Add the tick events of all Components in the scene to the scene event queue.
         */
        void AddTickEvent();

        /**
         * @brief Create a new GameObject in the scene.
         * The adding operation is queued and processed via Scene::FlushCmdQueue().
         * @return The reference to the created GameObject.
         */
        GameObject &CreateGameObject();

        /**
         * @brief Create a new Component in the scene.
         * The adding operation is queued and processed via Scene::FlushCmdQueue().
         * @param parent The parent GameObject of the Component.
         * @param type The type of the Component.
         * @return The reference to the created Component.
         */
        Component &CreateComponent(GameObject &parent, const Reflection::Type &type);

        /**
         * @brief Create a new Component in the scene.
         * The adding operation is queued and processed via Scene::FlushCmdQueue().
         * @param parent The parent GameObject of the Component.
         * @tparam T T must be derived from Component
         * @return The reference to the created Component.
         */
        template <typename T>
        T &CreateComponent(GameObject &parent) {
            static_assert(std::is_base_of<Component, T>::value, "T must be derived from Component");
            return static_cast<T &>(AddComponent(parent, new T(parent)));
        }

        /**
         * @brief Remove a GameObject from the scene.
         * The remove operation is queued and processed via Scene::FlushCmdQueue().
         * @param handle The GameObject handle to remove.
         */
        void RemoveGameObject(ObjectHandle handle);

        /**
         * @brief Remove a Component from the scene.
         * The remove operation is queued and processed via Scene::FlushCmdQueue().
         * @param handle The Component handle to remove.
         */
        void RemoveComponent(ComponentHandle handle);

        /**
         * @brief Load or remove all GameObjects or Components in the command queue.
         */
        void FlushCmdQueue();

        /**
         * @brief Process all events in the event queue.
         */
        void ProcessEvents();

        /**
         * @brief Clear all events in the event queue.
         */
        void ClearEventQueue();

        /**
         * @brief Get a GameObject in the scene by its handle.
         * @param handle The GameObject handle.
         * @return The GameObject pointer.
         */
        GameObject *GetGameObject(ObjectHandle handle) const;

        /**
         * @brief Get a Component in the scene by its handle.
         * @param handle The Component handle.
         * @return The Component pointer.
         */
        Component *GetComponent(ComponentHandle handle) const;

        /**
         * @brief Get a Component in the scene by its handle.
         * @param handle The Component handle.
         * @return The Component pointer.
         */
        template <typename T>
        T *GetComponent(ComponentHandle handle) const {
            return dynamic_cast<T *>(GetComponent(handle));
        }

        /**
         * @brief Get a reference to a GameObject in the scene by its handle.
         * @param handle The GameObject handle.
         * @return The reference to the GameObject.
         */
        GameObject &GetGameObjectRef(ObjectHandle handle) const;

        /**
         * @brief Get a reference to a Component in the scene by its handle.
         * @param handle The Component handle.
         * @return The reference to the Component.
         */
        Component &GetComponentRef(ComponentHandle handle) const;

        /**
         * @brief Get a reference to a Component in the scene by its handle.
         * @param handle The Component handle.
         * @return The reference to the Component.
         */
        template <typename T>
        T &GetComponentRef(ComponentHandle handle) const {
            return dynamic_cast<T &>(GetComponentRef(handle));
        }

        /**
         * @brief Get all GameObjects in the scene.
         * @return A vector of pointers to the GameObjects.
         */
        const std::vector<std::unique_ptr<GameObject>> &GetGameObjects() const;

        /**
         * @brief Get all Components in the scene.
         * @return A vector of pointers to the Components.
         */
        const std::vector<std::unique_ptr<Component>> &GetComponents() const;

        Scene(const Scene &) = delete;
        Scene &operator=(const Scene &) = delete;
        Scene(Scene &&) = delete;
        Scene &operator=(Scene &&) = delete;

        /**
         * @brief Clear all GameObjects and Components in the scene.
         */
        void Clear();

    protected:
        uint32_t m_sceneID;

        std::vector<std::unique_ptr<GameObject>> m_go_add_queue{};
        std::vector<ObjectHandle> m_go_remove_queue{};
        std::vector<std::unique_ptr<Component>> m_comp_add_queue{};
        std::vector<ComponentHandle> m_comp_remove_queue{};

        std::vector<std::unique_ptr<GameObject>> m_game_objects{};
        std::vector<std::unique_ptr<Component>> m_components{};

        std::unordered_map<ObjectHandle, GameObject *> m_go_map{};
        std::unordered_map<ComponentHandle, Component *> m_comp_map{};

        std::unique_ptr<EventQueue> m_event_queue{};

        uint32_t m_comp_id_gen{0};
        uint32_t m_go_id_gen{0};

    protected:
        // GameObject need to access AddComponent function
        friend class GameObject;

        /**
         * @brief Add a Component to the GameObject.
         * @param parent The parent GameObject of the Component.
         * @param ptr The Component pointer.
         * @return The reference to the created Component.
         */
        Component &AddComponent(GameObject &parent, Component *ptr);

        /**
         * @brief Get the next available Component handle of this scene.
         * @return The next available Component handle.
         */
        ComponentHandle NextAvailableComponentHandle();

        /**
         * @brief Get the next available GameObject handle of this scene.
         * @return The next available GameObject handle.
         */
        ObjectHandle NextAvailableGameObjectHandle();
    };
} // namespace Engine

#endif // FRAMEWORK_WORLD_SCENE_INCLUDED
