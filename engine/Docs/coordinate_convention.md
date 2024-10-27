# Corrdinate Convention

Our _coordinate system_ is in accord with _Blender_.
We choose a right-handed cartesian frame, with X pointing right of the screen, Y pointing front (pointing into the screen) and Z pointing up.
Namely, this is a LFU coordinate system.

Note that the screen Corrdinate system for Vulkan is defined as: X pointing to the right of the screen, Y pointing to the bottom of the screen, and Z is chosen when specifying depth comparison operation.
If the operation is chosen as `VK_COMPARE_OP_LESS`, then fragments with lower depth values will be presented, and therefore Z points inwards to the screen.

## Rotation

Our _rotation_ is in general represented by quaternions, supported by GLM library and stored in w-x-y-z order.
For euler angle representations, we follow the aircraft principle axes convention.
The angles should be saved in pitch-roll-yaw order in degrees, and the rotation is applied in Z-Y-X order.
For axis-angle representations of rotation, we use degrees units.

## Order of transforms

The order of _transforms_, consist of translation, rotation and scale, is as follows.
First, scaling is applied.
Then, the frame is rotated.
Finally, translation is applied.
This procedure produces model matrix satisfying
$$
M = T R S
$$
To provide an example, a point located at origin after a transform of translation (1, 0, 0) and rotation (0, 0, 90) should find itself at (0, 1, 0).
