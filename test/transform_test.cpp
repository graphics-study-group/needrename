#include <iostream>
#include <cassert>
#include <random>

#include "Core/Math/Transform.h"

using namespace Engine;

std::mt19937 gen{};
std::uniform_real_distribution <float> translation_dist {-100.0, 100.0};
std::uniform_real_distribution <float> rotation_dist {-360.0, 360.0};
std::uniform_real_distribution <float> scale_dist {0.1, 10.0};

float QuaternionDistance(glm::quat q1, glm::quat q2) {
    return 1 - glm::dot(q1, q2) * glm::dot(q1, q2);
}

Transform RandomTransform () {
    glm::vec3 translation{translation_dist(gen), translation_dist(gen), translation_dist(gen)};
    glm::vec3 rotation{rotation_dist(gen), rotation_dist(gen), rotation_dist(gen)};
    glm::vec3 scale{scale_dist(gen), scale_dist(gen), scale_dist(gen)};

    Transform t;
    t.SetPosition(translation).SetRotationEuler(rotation).SetScale(scale);
    return t;
}

void test_composition() {
    Transform t1, t2;
    for (int i = 0; i < 100; i++) {
        t1 = RandomTransform();
        t2 = RandomTransform();

        glm::mat4 m1, m2, m3;
        m1 = t1.GetTransformMatrix();
        m2 = t2.GetTransformMatrix();

        Transform t3 = t2 * t1;
        m3 = t3.GetTransformMatrix();

        glm::vec4 origin {0.0, 0.0, 0.0, 1.0};
        glm::vec4 v1 = m2 * (m1 * origin);
        glm::vec4 v2 = m3 * origin;

        assert(glm::distance(v1, v2) <= 1e-3);
    }

    puts("Composition test passed.");
}

void test_decompose() {
    Transform t1, t2;
    for (int i = 0; i < 100; i++) {
        t1 = RandomTransform();
        t2 = RandomTransform();

        glm::mat4 m1, m2;
        m1 = t1.GetTransformMatrix();
        m2 = t2.GetTransformMatrix();

        Transform t4;
        t4.Decompose(m1);

        assert(glm::distance(t1.GetPosition(), t4.GetPosition()) <= 1e-3);
        assert(glm::distance(t1.GetScale(), t4.GetScale()) <= 1e-3);
    }

    puts("Decomposition test passed.");
}

void test_rotation() {
    Transform t1, t2;
    for (int i = 0; i < 100; i++) {
        float angle = rotation_dist(gen);
        // Rotate around X-axis
        t1.SetRotationAxisAngles(glm::vec3{1.0f * angle, 0.0f, 0.0f});
        t2.SetRotationEuler(glm::vec3{glm::radians(angle), 0.0f, 0.0f});

        glm::quat q1{t1.GetRotation()}, q2{t2.GetRotation()};
        float distance = QuaternionDistance(q1, q2);
        assert(distance <= 1e-3);
    }

    puts("Rotation test passed.");
}

int main(int argc, char * argv[])
{
    test_composition();
    test_decompose();
    test_rotation();
    return 0;
}
