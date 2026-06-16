## 1. RendererComponent::PreRenderUpdate — ancestor walk

- [x] 1.1 Replace the single `FindRigidBodyByObjectHandle(parentObj->GetHandle())` call with an ancestor-chain loop: start at `this->GetParentGameObject()`, traverse upward via `scene->GetGameObject(handle)` and `go->GetParent()`, calling `FindRigidBodyByObjectHandle` at each level until a valid index is found or the root is reached
- [x] 1.2 When a rigid body ancestor is found, compute the local transform: `glm::mat4 rb_world = ancestor_go->GetWorldTransform().GetTransformMatrix(); model = glm::inverse(rb_world) * model;`
- [x] 1.3 Add necessary includes for Scene, GameObject (GetParent, GetWorldTransform)

## 2. Vertex shader — compose local transform

- [x] 2.1 Modify `get_model_matrix()` in `builtin_assets/shaders/include/engine/interface.glsl`: change `return model_matrices.m[pc.model_mat_index];` to `return model_matrices.m[pc.model_mat_index] * pc.model;`

## 3. Build and verify

- [x] 3.1 Build the project and fix any compilation errors
- [x] 3.2 Run the physics example and verify: `model_mat_index` is no longer -1 for physics-driven boxes, boxes render at correct positions, simulation applies correctly
