# Interop of Shader with Material System

In the engine we use a frequency-based descriptor management system.
The descriptors are used in two sets:
1. *Set index 0* stores global per-scene uniforms, such as environmental light;
2. *Set index 1* stores per-view or per-camera uniforms, such as environment light direction, view and projection matrices;
3. *Set index 2* stores per-material uniforms, such as diffuse textures.
4. Model matrices are pushed to shader via *push constants*.

## Global Descriptor Management

Global descriptors are managed by `GlobalConstantDescriptorPool`, which is compounded in the `RenderSystem`.
Corresponding C/C++ structs, together with descriptor set layouts, are stored in `ConstantData` directory.

The `GlobalConstantDescriptorPool` stores host-side handles for a descriptor pool, descriptor sets and uniform buffers which are pointed to by the descriptors.
Due to multiple frames in flight, this class actually holds multiple copies of buffers, one for each frame-in-flight.

When `AllocateGlobalSets` is called, it allocates buffers for each frame-in-flight, maps these buffers persistently onto host memory, and the requests descriptor sets, one for each frame, with the layout provided in `ConstantData`.
Then, it writes out descriptor set with `vkUpdateDescriptorSets`, pointing these descriptors to the allocated buffers.

To update per camera global constants for example, call `GetPerCameraConstantMemory` with frame index to acquire the pointer to mapped host memory, write to it (perferably with `memcpy`), and flush buffer with `FlushPerCameraConstantMemory`.
This is wrapped in `RenderSystem::WritePerCameraConstants`.

To use these constants in shader, simply use layout directive with `set = 0|1`, for example:
```glsl
layout(set = 1, binding = 0) uniform CameraBuffer{
    mat4 view;
    mat4 proj;
} camera;
```

## Per-material Descriptor Management

Per-material descriptor management is a little more complex due to its flexibilty.
It is done via `MaterialDescriptorManager`, which holds a descriptor pool and a map from strings to descriptor layouts.

In your material class, to manipulate descriptors, first create an array of bindings and call `NewDescriptorSetLayout` to create a mapping from a custom name to a descriptor layout, then call `AllocateDescriptorSet` with your name to allocate a descriptor set with the given layout.
You don't need to worry about cleaning up these sets, as they are automatically freed when the descriptor pool is destroyed.

To update these descriptors, you will have to override `WriteDescriptor` virtual function and call `vkUpdateDescriptorSets` in it.
It is called in the `RenderCommandBuffer::BindMaterial` before actual drawing.
Typically the descriptors should not be updated frequently, and you can mannually update the descriptor set by exposing new interfaces.

To access these uniforms in shader, use layout directive with `set = 2`, for example:
```glsl
layout(set = 2, binding = 0) uniform sampler2D Sampler;
```

## Per-model Constant

Currently per-model constants supports only model matrices via push constant, you can access them with `push_constant` layout directive.
