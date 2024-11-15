#include "Object.h"
#include <MainClass.h>
#include <Framework/world/WorldSystem.h>

namespace Engine
{
    Object::Object()
    {
        m_id = MainClass::GetInstance()->GetWorldSystem()->GenerateID();
    }
}
