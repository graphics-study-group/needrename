#include "LightComponent.h"

namespace Engine
{
    LightComponent::LightComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject) {
    }
}
