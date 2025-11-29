# Coordinate Convention

Our _world coordinate system_ is in accord with _Blender_.
We choose a right-handed cartesian frame, with X pointing right of the screen, Y pointing front (i.e. pointing into the screen) and Z pointing up.
Namely, this is a RFU coordinate system.

The camera in the world are always set to look at the front direction (Y+).

## Rotation

Our _rotation_ is in general represented by quaternions, supported by GLM library and stored in w-x-y-z order.

For euler angle representations, the convertion is done with the help of `glm::quat(glm::vec3)` constructor, and follows the exactly same convention.
As of writing, the angles should be saved in _X-Y-Z_ order in _radians_, and the rotation is applied in such order.

For axis-angle representations of rotation, we use degrees as units.

## Order of transforms

The order of _transforms_, consist of translation, rotation and scale, is as follows.
First, scaling is applied.
Then, the frame is rotated.
Finally, translation is applied.
This procedure produces model matrix satisfying
$$
M = T R S
$$

## Implementation Details

### Camera View Space

Note that the screen coordinate system (i.e. normalized device coordinate) for Vulkan is defined as: X pointing to the right of the screen, Y pointing to the top of the screen, and Z is chosen when specifying depth comparison operation.
If the operation is chosen as `VK_COMPARE_OP_LESS`, then fragments with lower depth values will be presented, and therefore Z points inwards to the screen.
This convention is somewhat different from our world space covention, where Y+ points to front.

A corollary follows that if the view space of a camera and the world space is identified, namely, the view matrix is the identity matrix, and the depth comparision operation is chosen to be less or less and equal, then the camera will be looking at the bottom of the world (Z-).

However, if you set the view matrix of the camera with `UpdateViewMatrix(Transform)` method, then the view matrix will be automatically generated as if the camera is facing towards the Y+ direction (i.e. front) in its local frame.
For euler angle, this specifically means that while they are in _X-Y-Z_ order, their meaning is not _pitch-yaw-roll_ as in the Vulkan NDC space, but _pitch-roll-yaw_ as in the world space.
Therefore, for your sanity, you should _avoid_ directly manipulating the view matrix, and always use the abstracted `Transform` interface.
