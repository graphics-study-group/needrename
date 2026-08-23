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
