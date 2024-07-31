#include <SDL3/SDL.h>
#include <cassert>
#include <random>

#include "Core/Math/Transform.h"

using namespace Engine;

std::mt19937 gen{};
std::uniform_real_distribution <float> translation_dist {-100.0, 100.0};
std::uniform_real_distribution <float> rotation_dist {-360.0, 360.0};
std::uniform_real_distribution <float> scale_dist {0.1, 10.0};

Transform RandomTransform () {
    glm::vec3 translation{translation_dist(gen), translation_dist(gen), translation_dist(gen)};
    glm::vec3 rotation{rotation_dist(gen), rotation_dist(gen), rotation_dist(gen)};
    glm::vec3 scale{scale_dist(gen), scale_dist(gen), scale_dist(gen)};

    Transform t;
    t.SetPosition(translation).SetRotationEuler(rotation).SetScale(scale);
    return t;
}

int main(int argc, char * argv[])
{
    Transform t1, t2;
    for (int i = 0; i < 100; i++) {
        t1 = RandomTransform();
        t2 = RandomTransform();

        glm::mat4 m1, m2, m3;
        m1 = t1.GetTransformMatrix();
        m2 = t2.GetTransformMatrix();

        Transform t4;
        t4.Decompose(m1);

        assert(glm::distance(t1.GetPosition(), t4.GetPosition()) <= 1e-4);
        assert(glm::distance(t1.GetRotationAxisAngles(), t4.GetRotationAxisAngles()) <= 1e-4);
        assert(glm::distance(t1.GetScale(), t4.GetScale()) <= 1e-4);

        Transform t3 = t2 * t1;
        m3 = t3.GetTransformMatrix();

        glm::vec4 origin {0.0, 0.0, 0.0, 1.0};
        glm::vec4 v1 = m2 * (m1 * origin);
        glm::vec4 v2 = m3 * origin;

        printf("v1: %f, %f, %f, %f\t", v1.x, v1.y, v1.z, v1.w);
        printf("v2: %f, %f, %f, %f\n", v2.x, v2.y, v2.z, v2.w);
        assert(glm::distance(v1, v2) <= 1e-4);
    }
    return 0;
}
