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
        /**
         * @brief Base class for all handles.
         * Handles are used to identify GameObjects and Components in the scene.
         * It contains the scene ID and the ID of the GameObject or Component.
         * IDs are usually assigned in integer order
         */
        class HandleBase {
        public:
            HandleBase() = default;
            HandleBase(uint32_t ID);
            virtual ~HandleBase() = default;

            /**
             * @brief Get the scene ID.
             * @return The scene ID.
             */
            virtual uint32_t GetSceneID() const noexcept;

            /**
             * @brief Get the ID of the GameObject or Component.
             * @return The ID of the GameObject or Component.
             */
            virtual uint32_t GetID() const noexcept;

            /**
             * @brief Check if the handle is valid (ID is not zero).
             * @return True if the handle is valid, False otherwise.
             */
            virtual bool IsValid() const noexcept;

            /**
             * @brief Reset the handle to a null handle (ID is zero).
             */
            virtual void Reset() noexcept;

            /**
             * @brief Check if the handles are equal.
             * Only check the ID. Ignore the scene ID.
             * @param other The other handle to compare with.
             * @return True if the handles are equal, False otherwise.
             */
            bool operator==(const HandleBase &other) const noexcept;

            /**
             * @brief Custom serialization function.
             * Save the ID only. Ignore the scene ID.
             */
            virtual void save_to_archive(Engine::Serialization::Archive &archive) const;

            /**
             * @brief Custom deserialization function.
             * Load the ID only. Ignore the scene ID.
             */
            virtual void load_from_archive(Engine::Serialization::Archive &) = 0;

        protected:
            uint32_t m_sceneID{};
            uint32_t m_ID{};

            HandleBase(uint32_t sceneID, uint32_t ID);
        };
    } // namespace detail

    /**
     * @brief Handle for GameObjects.
     * It contains the scene ID and the ID of the GameObject.
     * IDs are usually assigned in integer order
     */
    class ObjectHandle : public detail::HandleBase {
    public:
        ObjectHandle() = default;
        ObjectHandle(uint32_t ID);

        /**
         * @brief Get the GameObject associated with the handle.
         * It will try to get the Scene from WorldSystem via SceneID.
         * And try to get the GameObject from the Scene via ID.
         * @return The GameObject associated with the handle. nullptr if not found.
         */
        GameObject *GetGameObject() const;

        /**
         * @brief Custom deserialization function.
         * Use HandleResolver to load handle.
         * HandleResolver can manage the ObjectHandle in different scenes.
         */
        virtual void load_from_archive(Engine::Serialization::Archive &archive) override;

    protected:
        friend class Scene;
        ObjectHandle(uint32_t sceneID, uint32_t ID);
    };

    /**
     * @brief Handle for Components.
     * It contains the scene ID and the ID of the Component.
     * IDs are usually assigned in integer order
     */
    class ComponentHandle : public detail::HandleBase {
    public:
        ComponentHandle() = default;
        ComponentHandle(uint32_t ID);

        /**
         * @brief Get the Component associated with the handle.
         * It will try to get the Scene from WorldSystem via SceneID.
         * And try to get the Component from the Scene via ID.
         * @return The Component associated with the handle. nullptr if not found.
         */
        Component *GetComponent() const;

        /**
         * @brief Custom deserialization function.
         * Use HandleResolver to load handle.
         * HandleResolver can manage the ComponentHandle in different scenes.
         */
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
