#ifndef APPPHYSICS_PHYSICSAPP_H
#define APPPHYSICS_PHYSICSAPP_H

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace AppPhysics {

    /**
     * @brief Options for creating the physics app.
     *
     * Only window parameters are exposed. Asset and project paths are read
     * internally from cmake_config.h for this iteration.
     */
    struct CreateInfo {
        uint32_t resol_x{1280};
        uint32_t resol_y{720};
        std::string window_title{"Physics App"};
    };

    /// Opaque handle identifying a created body. Not a pointer; do not dereference.
    using BodyId = uint32_t;
    constexpr BodyId INVALID_BODY_ID = ~0u;

    /**
     * @brief Descriptor for creating a physics box object.
     *
     * @note `color` names a builtin solid color asset. Empty string selects a
     * random color at Add time; an invalid non-empty string throws
     * `std::invalid_argument`.
     */
    struct BoxDesc {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 half_extents{0.5f, 0.5f, 0.5f};
        float mass{1.0f};
        bool kinematic{false};
        float static_friction{0.5f};
        float dynamic_friction{0.5f};
        float restitution{0.0f};
        std::string color{};
    };

    /**
     * @brief Descriptor for creating a physics sphere object (Z-up world).
     */
    struct SphereDesc {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        float radius{0.5f};
        float mass{1.0f};
        bool kinematic{false};
        float static_friction{0.5f};
        float dynamic_friction{0.5f};
        float restitution{0.0f};
        std::string color{};
    };

    /**
     * @brief Descriptor for creating a physics cylinder object (Z-up axis).
     */
    struct CylinderDesc {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        float radius{0.5f};
        float half_height{0.5f};
        float mass{1.0f};
        bool kinematic{false};
        float static_friction{0.5f};
        float dynamic_friction{0.5f};
        float restitution{0.0f};
        std::string color{};
    };

    /**
     * @brief Parameters for a fixed joint.
     *
     * @note obj1 (passed to PhysicsApp::AddFixedJoint) is the owning body;
     * its initial relative transform to obj2 is derived at Awake time.
     */
    struct FixedJointParams {
        float compliance{0.0f}; ///< Joint compliance (0 = hard constraint).
    };

    /**
     * @brief Parameters for a hinge joint.
     *
     * Axis and anchor are expressed in obj1's local frame.
     */
    struct HingeJointParams {
        glm::vec3 axis_obj1{1.0f, 0.0f, 0.0f};   ///< Hinge axis in obj1's local frame.
        glm::vec3 anchor_obj1{0.0f, 0.0f, 0.0f}; ///< Hinge anchor point in obj1's local frame.
        float compliance{0.0f};                  ///< Joint compliance (0 = hard constraint).
    };

    /**
     * @brief Parameters for a directional light.
     *
     * The light is not a physics body. `color` is an rgb linear value.
     */
    struct DirectionalLightParams {
        glm::vec3 direction{0.0f, 0.0f, -1.0f}; ///< Light direction in world space.
        glm::vec3 color{1.0f, 1.0f, 1.0f};      ///< rgb linear color.
        float intensity{1.0f};
        bool cast_shadow{true};
    };

    /**
     * @brief Minimal entry point for GPU physics simulation.
     *
     * Lifecycle:
     *   1. Create(info) - full initialization (window, assets, project, input axes).
     *   2. Building phase - add bodies, joints, lights, set camera pose.
     *   3. CommitScene() - one-way freeze; afterwards no scene changes are allowed.
     *   4. Drive phase - Step() / RenderNextFrame() / Pause() / Resume() / ShouldQuit().
     *
     * Wrong-phase usage throws `std::logic_error`. Exceptions may cross the
     * DLL boundary; a future C API must convert them to error codes.
     */
    class PhysicsApp {
    public:
        /**
         * @brief Create a physics app with full initialization.
         *
         * Internally initializes MainClass, loads builtin assets and the
         * `empty_with_sky` project, registers input axes, and sets up the scene
         * builder and the built-in camera.
         *
         * @param info Create options (window resolution and title).
         * @return Unique-ownership instance of the app.
         * @throws std::runtime_error when initialization fails.
         */
        static std::unique_ptr<PhysicsApp> Create(const CreateInfo &info);

        /**
         * @brief Destroy the physics app.
         *
         * The held MainClass is released last so engine subsystems outlive the
         * app.
         */
        ~PhysicsApp();
        PhysicsApp(const PhysicsApp &) = delete;
        PhysicsApp &operator=(const PhysicsApp &) = delete;
        PhysicsApp(PhysicsApp &&) = delete;
        PhysicsApp &operator=(PhysicsApp &&) = delete;

        // ── Building phase ──────────────────────────────────────────────

        /**
         * @brief Add a physics box to the scene (Building phase only).
         *
         * @param desc Box configuration.
         * @return BodyId identifying the created body.
         * @throws std::logic_error after CommitScene.
         */
        BodyId AddBox(const BoxDesc &desc);

        /**
         * @brief Add a physics sphere to the scene (Building phase only).
         *
         * @param desc Sphere configuration.
         * @return BodyId identifying the created body.
         * @throws std::logic_error after CommitScene.
         */
        BodyId AddSphere(const SphereDesc &desc);

        /**
         * @brief Add a physics cylinder to the scene (Building phase only).
         *
         * @param desc Cylinder configuration.
         * @return BodyId identifying the created body.
         * @throws std::logic_error after CommitScene.
         */
        BodyId AddCylinder(const CylinderDesc &desc);

        /**
         * @brief Add a fixed joint between two bodies (Building phase only).
         *
         * @param obj1   Owning body (hosts the constraint component).
         * @param obj2   Referenced body.
         * @param params Joint parameters.
         * @throws std::logic_error after CommitScene.
         * @throws std::out_of_range when a BodyId is invalid.
         */
        void AddFixedJoint(BodyId obj1, BodyId obj2, const FixedJointParams &params);

        /**
         * @brief Add a hinge joint between two bodies (Building phase only).
         *
         * @param obj1   Owning body (hosts the constraint component).
         * @param obj2   Referenced body.
         * @param params Joint parameters (axis, anchor, compliance).
         * @throws std::logic_error after CommitScene.
         * @throws std::out_of_range when a BodyId is invalid.
         */
        void AddHingeJoint(BodyId obj1, BodyId obj2, const HingeJointParams &params);

        /**
         * @brief Add a directional light (Building phase only).
         *
         * @param params Light parameters (direction, color, intensity, shadow).
         * @throws std::logic_error after CommitScene.
         */
        void AddDirectionalLight(const DirectionalLightParams &params);

        /**
         * @brief Set the camera pose to look at a target.
         *
         * Legal in both phases; the camera is excluded from the scene freeze.
         * When called after CommitScene the pose is applied immediately.
         *
         * @param position    Camera world position.
         * @param look_target World-space point the camera looks at.
         */
        void SetCameraPose(const glm::vec3 &position, const glm::vec3 &look_target);

        /**
         * @brief Commit and freeze the scene.
         *
         * Flushes creation commands, runs component init, syncs physics GPU
         * buffers, forwards model matrices to the renderer, builds the default
         * render graph and freezes the scene. Simulation starts paused.
         *
         * @throws std::logic_error when called more than once.
         */
        void CommitScene();

        // ── Drive phase (legal after CommitScene) ──────────────────────

        /**
         * @brief Advance the physics simulation by one fixed step.
         *
         * Uses a dedicated command buffer with device-level waits before and
         * after; does not process input. Consecutive calls are allowed and the
         * call is a no-op while paused.
         *
         * @throws std::logic_error before CommitScene.
         */
        void Step();

        /**
         * @brief Render one frame, processing input and updating the camera.
         *
         * Polls SDL events, updates input and the built-in camera, updates
         * renderer data, then renders a frame. The frame is skipped without
         * throwing when the swapchain is out of date (e.g. window minimized).
         *
         * @throws std::logic_error before CommitScene.
         */
        void RenderNextFrame();

        /**
         * @brief Pause the simulation (rendering continues).
         *
         * @throws std::logic_error before CommitScene.
         */
        void Pause();

        /**
         * @brief Resume the simulation.
         *
         * @throws std::logic_error before CommitScene.
         */
        void Resume();

        /**
         * @brief Check whether the simulation is paused.
         *
         * @return True if the simulation is paused.
         */
        bool IsPaused() const;

        // ── Legal in both phases ────────────────────────────────────────

        /**
         * @brief Check whether the app should quit.
         *
         * @return True when an SDL_QUIT (window close) event has been received.
         */
        bool ShouldQuit() const;

    private:
        PhysicsApp();
        struct Impl;
        std::unique_ptr<Impl> m_impl{};
    };
} // namespace AppPhysics

#endif // APPPHYSICS_PHYSICSAPP_H
