#include "Actuator.h"

#include <glm.hpp>
#include <gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace AppPhysics {
    namespace {
        /// @brief Wrap an angle into [-pi, pi).
        float WrapAngle(float angle) {
            angle = std::fmod(angle, 2.0f * glm::pi<float>());
            if (angle > glm::pi<float>()) {
                angle -= 2.0f * glm::pi<float>();
            } else if (angle < -glm::pi<float>()) {
                angle += 2.0f * glm::pi<float>();
            } else if (std::fabs(angle - glm::pi<float>()) < 1.0e-10f) {
                angle = -glm::pi<float>();
            }
            return angle;
        }
    } // namespace

    void Actuator::SetTargetAngle(float target) {
        m_target_angle = target;
    }

    float Actuator::GetTargetAngle() const {
        return m_target_angle;
    }

    PdActuator::PdActuator(float kp, float kd) : m_kp(kp), m_kd(kd) {
    }

    float PdActuator::ComputeTorque(float angle, float angular_velocity) {
        const float error = WrapAngle(m_target_angle - angle);
        return m_kp * error - m_kd * angular_velocity;
    }

    float PdActuator::GetKp() const {
        return m_kp;
    }

    void PdActuator::SetKp(float kp) {
        m_kp = kp;
    }

    float PdActuator::GetKd() const {
        return m_kd;
    }

    void PdActuator::SetKd(float kd) {
        m_kd = kd;
    }

    DcMotorActuator::DcMotorActuator(
        float kp, float kd, float stall_torque, float no_load_speed, float cont_torque, float gear_ratio
    ) :
        m_kp(kp), m_kd(kd), m_stall_torque(stall_torque), m_no_load_speed(no_load_speed), m_cont_torque(cont_torque),
        m_gear_ratio(gear_ratio) {
        UpdateCornerSpeed();
    }

    float DcMotorActuator::ComputeTorque(float angle, float angular_velocity) {
        // PD core.
        const float error = WrapAngle(m_target_angle - angle);
        float tau = m_kp * error - m_kd * angular_velocity;

        // DC-motor torque-speed envelope (motor-side velocity).
        const float omega_motor = angular_velocity / m_gear_ratio;
        const float omega_max_motor = m_no_load_speed / m_gear_ratio;
        const float corner_motor = m_corner_speed / m_gear_ratio;
        const float omega_clipped = std::clamp(omega_motor, -corner_motor, corner_motor);

        const float torque_speed_upper = m_stall_torque * (1.0f - omega_clipped / omega_max_motor);
        const float torque_speed_lower = m_stall_torque * (-1.0f - omega_clipped / omega_max_motor);

        const float max_torque = std::min(torque_speed_upper, m_cont_torque);
        const float min_torque = std::max(torque_speed_lower, -m_cont_torque);

        return std::clamp(tau, min_torque, max_torque);
    }

    float DcMotorActuator::GetKp() const {
        return m_kp;
    }

    void DcMotorActuator::SetKp(float kp) {
        m_kp = kp;
    }

    float DcMotorActuator::GetKd() const {
        return m_kd;
    }

    void DcMotorActuator::SetKd(float kd) {
        m_kd = kd;
    }

    float DcMotorActuator::GetStallTorque() const {
        return m_stall_torque;
    }

    void DcMotorActuator::SetStallTorque(float stall_torque) {
        m_stall_torque = stall_torque;
        UpdateCornerSpeed();
    }

    float DcMotorActuator::GetNoLoadSpeed() const {
        return m_no_load_speed;
    }

    void DcMotorActuator::SetNoLoadSpeed(float no_load_speed) {
        m_no_load_speed = no_load_speed;
        UpdateCornerSpeed();
    }

    float DcMotorActuator::GetContTorque() const {
        return m_cont_torque;
    }

    void DcMotorActuator::SetContTorque(float cont_torque) {
        m_cont_torque = cont_torque;
        UpdateCornerSpeed();
    }

    float DcMotorActuator::GetGearRatio() const {
        return m_gear_ratio;
    }

    void DcMotorActuator::SetGearRatio(float gear_ratio) {
        m_gear_ratio = gear_ratio;
    }

    void DcMotorActuator::UpdateCornerSpeed() {
        m_corner_speed = m_no_load_speed * (1.0f + m_cont_torque / std::max(m_stall_torque, 1.0e-10f));
    }
} // namespace AppPhysics
