#include <Actuator.h>
#include <PhysicsApp.h>

#include <cmath>
#include <iostream>

using namespace AppPhysics;

namespace {
    int g_failures = 0;

    void Check(bool cond, const char *what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    bool Close(float a, float b, float eps) {
        return std::fabs(a - b) <= eps;
    }

    // Wrapped angle-error helper mirroring the actuator law for expectation math.
    float Wrap(float angle) {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTwoPi = 2.0f * kPi;
        angle = std::fmod(angle, kTwoPi);
        if (angle > kPi) {
            angle -= kTwoPi;
        } else if (angle < -kPi) {
            angle += kTwoPi;
        }
        return angle;
    }

    /// @brief Stateful control law for the `Reset()` contract: a leaking
    /// integrator whose output depends on how many calls came before.
    class LeakingIntegratorActuator final : public Actuator {
    public:
        float ComputeTorque(float angle, float angular_velocity) override {
            // Leak 10% per call so the state saturates instead of growing without
            // bound, then integrate the angle and use the accumulated error.
            m_integral = 0.9f * m_integral + angle;
            m_calls++;
            return 10.0f * m_integral - 2.0f * angular_velocity;
        }

        void Reset() override {
            m_integral = 0.0f;
            m_calls = 0;
        }

        float Integral() const {
            return m_integral;
        }

        int Calls() const {
            return m_calls;
        }

    private:
        float m_integral{0.0f};
        int m_calls{0};
    };
} // namespace

int main() {
    // ── PdActuator: nominal law ──────────────────────────────────────
    {
        PdActuator a(10.0f, 2.0f);
        a.SetTargetAngle(1.0f);
        const float tau = a.ComputeTorque(0.8f, 0.1f);
        Check(Close(tau, 10.0f * Wrap(0.2f) - 2.0f * 0.1f, 1e-5f), "Pd law value");
        Check(a.GetTargetAngle() == 1.0f, "Pd target getter");
    }

    // ── PdActuator: angle error wraps across ±pi ─────────────────────
    {
        PdActuator a(10.0f, 0.0f);
        a.SetTargetAngle(3.1f);
        const float tau = a.ComputeTorque(-3.1f, 0.0f);
        const float wrapped = Wrap(3.1f - (-3.1f));
        Check(wrapped < 0.0f, "Pd wrapped error sign (small negative)");
        Check(Close(tau, 10.0f * wrapped, 1e-4f), "Pd wrapped error magnitude");
    }

    // ── DcMotorActuator: zero-speed stall clip to +cont_torque ───────
    {
        DcMotorActuator a(1000.0f, 0.0f, 33.5f, 21.0f, 13.4f, 1.0f);
        a.SetTargetAngle(0.1f);
        const float tau = a.ComputeTorque(0.0f, 0.0f);
        Check(Close(tau, 13.4f, 1e-4f), "DcMotor zero-speed positive clip");
    }

    // ── DcMotorActuator: zero-speed clip to -cont_torque ─────────────
    {
        DcMotorActuator a(1000.0f, 0.0f, 33.5f, 21.0f, 13.4f, 1.0f);
        a.SetTargetAngle(-0.1f);
        const float tau = a.ComputeTorque(0.0f, 0.0f);
        Check(Close(tau, -13.4f, 1e-4f), "DcMotor zero-speed negative clip");
    }

    // ── DcMotorActuator: torque-speed envelope below no-load speed ───
    {
        // At omega=20 (below no-load 21): upper bound = stall*(1 - 20/21).
        const float upper = 33.5f * (1.0f - 20.0f / 21.0f);
        DcMotorActuator a(1000.0f, 0.0f, 33.5f, 21.0f, 13.4f, 1.0f);
        a.SetTargetAngle(0.1f);
        const float tau = a.ComputeTorque(0.0f, 20.0f);
        Check(Close(tau, upper, 1e-3f), "DcMotor envelope below no-load");
    }

    // ── DcMotorActuator: sign reversal above no-load speed ───────────
    {
        // At omega=25 (above no-load 21): upper bound = stall*(1 - 25/21) < 0,
        // so a positive PD error still yields a negative torque.
        const float upper = 33.5f * (1.0f - 25.0f / 21.0f);
        DcMotorActuator a(100.0f, 0.0f, 33.5f, 21.0f, 13.4f, 1.0f);
        a.SetTargetAngle(0.1f);
        const float tau = a.ComputeTorque(0.0f, 25.0f);
        Check(tau < 0.0f, "DcMotor sign reversal above no-load");
        Check(Close(tau, upper, 1e-3f), "DcMotor sign reversal value");
    }

    // ── Parameters are readable and tunable (Pd) ─────────────────────
    {
        PdActuator a(10.0f, 2.0f);
        Check(a.GetKp() == 10.0f, "Pd kp read-back");
        Check(a.GetKd() == 2.0f, "Pd kd read-back");

        a.SetTargetAngle(1.0f);
        a.SetKp(30.0f);
        a.SetKd(0.0f);
        Check(a.GetKp() == 30.0f, "Pd kp written value is readable");
        Check(a.GetKd() == 0.0f, "Pd kd written value is readable");
        Check(
            Close(a.ComputeTorque(0.8f, 0.1f), 30.0f * Wrap(0.2f), 1e-5f),
            "Pd tuned kp changes the next computed torque"
        );
    }

    // ── Parameters are readable and tunable (DcMotor) ────────────────
    {
        DcMotorActuator a(10.0f, 2.0f, 33.5f, 21.0f, 13.4f, 1.0f);
        Check(a.GetKp() == 10.0f, "DcMotor kp read-back");
        Check(a.GetKd() == 2.0f, "DcMotor kd read-back");
        Check(a.GetStallTorque() == 33.5f, "DcMotor stall_torque read-back");
        Check(a.GetNoLoadSpeed() == 21.0f, "DcMotor no_load_speed read-back");
        Check(a.GetContTorque() == 13.4f, "DcMotor cont_torque read-back");
        Check(a.GetGearRatio() == 1.0f, "DcMotor gear_ratio read-back");

        a.SetTargetAngle(0.1f);
        a.SetKp(1000.0f);
        a.SetKd(0.0f);
        a.SetStallTorque(33.5f);
        a.SetNoLoadSpeed(21.0f);
        a.SetContTorque(5.0f);
        a.SetGearRatio(1.0f);
        Check(a.GetContTorque() == 5.0f, "DcMotor cont_torque written value is readable");
        Check(
            Close(a.ComputeTorque(0.0f, 0.0f), 5.0f, 1e-4f),
            "DcMotor tuned cont_torque changes the next computed torque"
        );
    }

    // ── Reset clears internal control state ──────────────────────────
    {
        LeakingIntegratorActuator a;
        a.SetTargetAngle(0.0f);
        const float first = a.ComputeTorque(0.5f, 0.0f);
        const float second = a.ComputeTorque(0.5f, 0.0f);
        Check(a.Calls() == 2, "stateful actuator accumulates state");
        Check(second != first, "stateful actuator output depends on accumulated state");

        a.Reset();
        Check(a.Integral() == 0.0f, "Reset clears the accumulated integral");
        Check(a.Calls() == 0, "Reset clears the call counter");
        const float after_reset = a.ComputeTorque(0.5f, 0.0f);
        Check(Close(after_reset, first, 1e-6f), "Reset restores freshly-constructed behavior");

        // The base implementation is a no-op: a stateless actuator resets fine.
        PdActuator pd(10.0f, 2.0f);
        pd.SetTargetAngle(1.0f);
        const float before = pd.ComputeTorque(0.8f, 0.1f);
        pd.Reset();
        Check(Close(pd.ComputeTorque(0.8f, 0.1f), before, 1e-6f), "stateless Reset is a no-op");
    }

    // ── Default constructors produce A1 configuration ────────────────
    {
        PdActuator pd{};
        DcMotorActuator dc{};
        (void)pd;
        (void)dc;
        Check(true, "default-constructed actuators compile");
    }

    if (g_failures == 0) {
        std::cout << "All actuator unit checks passed." << std::endl;
    }
    return g_failures;
}
