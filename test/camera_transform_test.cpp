#include "Framework/go/GameObject.h"
#include "Framework/component/RenderComponent/CameraComponent.h"

using namespace Engine;

const glm::vec4 ORIGIN{0.0f, 0.0f, 0.0f, 1.0f};
const glm::vec4 X_UNITY{1.0f, 0.0f, 0.0f, 1.0f};
const glm::vec4 Y_UNITY{0.0f, 1.0f, 0.0f, 1.0f};
const glm::vec4 Z_UNITY{0.0f, 0.0f, 1.0f, 1.0f};

void print_matrix(glm::mat4 m){
    printf("%+8.4f%+8.4f%+8.4f%+8.4f\n%+8.4f%+8.4f%+8.4f%+8.4f\n%+8.4f%+8.4f%+8.4f%+8.4f\n%+8.4f%+8.4f%+8.4f%+8.4f\n",
        m[0][0], m[1][0], m[2][0], m[3][0],
        m[0][1], m[1][1], m[2][1], m[3][1],
        m[0][2], m[1][2], m[2][2], m[3][2],
        m[0][3], m[1][3], m[2][3], m[3][3]
    );
}

void print_vectors(glm::vec3 o, glm::vec3 x, glm::vec3 y, glm::vec3 z) {
    printf("O: (%f, %f, %f), X: (%f, %f, %f), Y: (%f, %f, %f), Z: (%f, %f, %f)\n"
        ,o.x, o.y, o.z, x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z);
}

void test_translation() {
    auto go = std::make_shared<GameObject>();
    auto cp = std::make_shared<CameraComponent>(go);

    // eye space +x axis and +z axis align up with NDC +x axis and +z axis
    go->GetTransformRef().SetPosition(glm::vec3(0, -1, 0));
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
    auto go = std::make_shared<GameObject>();
    auto cp = std::make_shared<CameraComponent>(go);

    // eye space +x axis and +z axis align up with NDC -z axis and +y axis
    go->GetTransformRef().SetRotationEuler(glm::vec3(0, 0, glm::radians(-90.0f)));
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
    test_translation();
    test_rotation();
}
