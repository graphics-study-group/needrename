#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <iostream>

#include <Core/Math/Transform.h>
#include <MainClass.h>
#include <Render/Renderer/Camera.h>
#include <cmake_config.h>

using namespace Engine;

const glm::vec4 ORIGIN{0.0f, 0.0f, 0.0f, 1.0f};
const glm::vec4 X_UNITY{1.0f, 0.0f, 0.0f, 1.0f};
const glm::vec4 Y_UNITY{0.0f, 1.0f, 0.0f, 1.0f};
const glm::vec4 Z_UNITY{0.0f, 0.0f, 1.0f, 1.0f};

// clang-format off
void print_matrix(glm::mat4 m) {
    printf("%+8.4f%+8.4f%+8.4f%+8.4f\n%+8.4f%+8.4f%+8.4f%+8.4f\n%+8.4f%+8.4f%+8.4f%+8.4f\n%+8.4f%+8.4f%+8.4f%+8.4f\n",
        m[0][0], m[1][0], m[2][0], m[3][0],
        m[0][1], m[1][1], m[2][1], m[3][1],
        m[0][2], m[1][2], m[2][2], m[3][2],
        m[0][3], m[1][3], m[2][3], m[3][3]
    );
}
// clang-format on

void print_vectors(glm::vec3 o, glm::vec3 x, glm::vec3 y, glm::vec3 z) {
    printf(
        "O: (%f, %f, %f), X: (%f, %f, %f), Y: (%f, %f, %f), Z: (%f, %f, %f)\n",
        o.x,
        o.y,
        o.z,
        x.x,
        x.y,
        x.z,
        y.x,
        y.y,
        y.z,
        z.x,
        z.y,
        z.z
    );
}

void test_translation() {
    Transform transform;
    // eye space +x axis and +z axis align up with NDC +x axis and +z axis
    transform.SetPosition(glm::vec3(0, -1, 0));

    auto cp = std::make_shared<Camera>();
    cp->UpdateViewMatrix(transform);

    glm::mat4 view{cp->GetViewMatrix()}, proj{cp->GetProjectionMatrix()};

    glm::vec4 o, x, y, z;
    o = view * ORIGIN;
    x = view * X_UNITY;
    y = view * Y_UNITY;
    z = view * Z_UNITY;

    assert(glm::distance(glm::vec3{x}, glm::vec3{1.0f, 0.0f, -1.0f}) <= 1e-3);
    assert(glm::distance(glm::vec3{y}, glm::vec3{0.0f, 0.0f, -2.0f}) <= 1e-3);
    assert(glm::distance(glm::vec3{z}, glm::vec3{0.0f, 1.0f, -1.0f}) <= 1e-3);

    o = proj * o;
    x = proj * x;
    y = proj * y;
    z = proj * z;
    print_vectors(o, x, y, z);
}

void test_rotation() {
    Transform transform;
    // eye space +x axis and +z axis align up with NDC -z axis and +y axis
    transform.SetRotationEuler(glm::vec3(0, 0, glm::radians(-90.0f)));
    auto cp = std::make_shared<Camera>();
    cp->UpdateViewMatrix(transform);
    glm::mat4 view{cp->GetViewMatrix()}, proj{cp->GetProjectionMatrix()};

    glm::vec4 o, x, y, z;
    o = view * ORIGIN;
    x = view * X_UNITY;
    y = view * Y_UNITY;
    z = view * Z_UNITY;
    assert(glm::distance(glm::vec3{x}, glm::vec3{0.0f, 0.0f, -1.0f}) <= 1e-3);
    assert(glm::distance(glm::vec3{y}, glm::vec3{-1.0f, 0.0f, 0.0f}) <= 1e-3);
    assert(glm::distance(glm::vec3{z}, glm::vec3{0.0f, 1.0f, 0.0f}) <= 1e-3);

    o = proj * o;
    x = proj * x;
    y = proj * y;
    z = proj * z;
    print_vectors(o, x, y, z);
}

int main() {
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "External Resource Loading Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    test_translation();
    test_rotation();

    return 0;
}
