
Our _coordinate system_ is in accord with _Blender_.
We choose a right-handed cartesian frame, with X pointing left, Y pointing front and Z pointing up.
Namely, this is a LFU coordinate system.

Our _rotation_ is in general represented by quaternions, supported by GLM library and stored in w-x-y-z order.
For euler angle representations, we follow the aircraft principle axes convention.
The angles should be saved in pitch-roll-yaw order in degrees, and the rotation is applied in Z-Y-X order.
For axis-angle representations of rotation, we use degrees units.

The order of _transforms_, consist of translation, rotation and scale, is as follows.
First, scaling is applied.
Then, the frame is rotated.
Finally, translation is applied.
This procedure produces model matrix satisfying
$$
M = T R S
$$
To provide an example, a point located at origin after a transform of translation (1, 0, 0) and rotation (0, 0, 90) sould find itself at (0, 1, 0).
