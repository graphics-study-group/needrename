#ifndef APPPHYSICS_ACTUATOR_H
#define APPPHYSICS_ACTUATOR_H

namespace AppPhysics {

    /**
     * @brief Base class for a scalar joint control law.
     *
     * An actuator holds a target joint angle and converts a measured joint state
     * (angle and angular velocity) into a torque about the joint axis. It is a
     * pure scalar function: it knows nothing about bodies, joints, or the physics
     * engine — `PhysicsApp` reads the joint state, calls `ComputeTorque`, and
     * applies the resulting torque to the engine.
     *
     * Target angles are expressed relative to the robot's loaded initial pose
     * (the angle at load is defined as 0).
     *
     * Derive from this class to add new actuator types; registering a derived
     * instance requires no change to `PhysicsApp`'s API. Actuators that need a
     * time step receive it as a constructor parameter.
     */
    class Actuator {
    public:
        Actuator() = default;
        virtual ~Actuator() = default;
        Actuator(const Actuator &) = delete;
        Actuator &operator=(const Actuator &) = delete;

        /**
         * @brief Compute the joint torque from the current joint state.
         *
         * @param angle            Current signed joint angle [rad], relative to the initial pose.
         * @param angular_velocity Current joint angular velocity [rad/s].
         * @return Torque [N·m] about the joint axis (positive rotates the child
         *         about the positive axis relative to the parent).
         */
        virtual float ComputeTorque(float angle, float angular_velocity) const = 0;

        /**
         * @brief Set the target joint angle [rad] (relative to the initial pose).
         */
        void SetTargetAngle(float target);

        /**
         * @brief Get the target joint angle [rad].
         */
        float GetTargetAngle() const;

    protected:
        float m_target_angle{0.0f};
    };
    /**
     * @brief Standard PD (proportional–derivative) joint actuator.
     *
     * \f$ \tau = kp \cdot wrap(target - angle) + kd \cdot (0 - \omega) \f$
     * where `wrap` maps the angle error into \f$ [-\pi, \pi) \f$.
     */
    class PdActuator : public Actuator {
    public:
        explicit PdActuator(float kp = 25.0f, float kd = 0.5f);

        float ComputeTorque(float angle, float angular_velocity) const override;

    private:
        float m_kp;
        float m_kd;
    };

    /**
     * @brief DCMotor joint actuator: PD core plus a four-quadrant DC-motor
     * torque-speed envelope.
     *
     * The raw PD torque is clipped to the motor's torque-speed characteristic at
     * the current joint velocity (scaled to the motor side by `gear_ratio`),
     * bounded by `±cont_torque`. Defaults are the Unitree A1 joint-side values.
     */
    class DcMotorActuator : public Actuator {
    public:
        explicit DcMotorActuator(
            float kp = 25.0f,
            float kd = 0.5f,
            float stall_torque = 33.5f,
            float no_load_speed = 21.0f,
            float cont_torque = 13.4f,
            float gear_ratio = 1.0f
        );

        float ComputeTorque(float angle, float angular_velocity) const override;

    private:
        float m_kp;
        float m_kd;
        float m_stall_torque;
        float m_no_load_speed;
        float m_cont_torque;
        float m_gear_ratio;
        float m_corner_speed;
    };

} // namespace AppPhysics

#endif // APPPHYSICS_ACTUATOR_H
