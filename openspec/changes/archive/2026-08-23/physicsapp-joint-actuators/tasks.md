# Tasks: PhysicsApp Joint Actuators

## 1. Engine: expose hinge axis on UrdfBuiltJoint

- [x] 1.1 Add `glm::vec3 axis{1.0f, 0.0f, 0.0f};` field to `UrdfBuiltJoint` in `engine/Framework/Import/UrdfTypes.h` with a doc comment (parent-link GO frame, engine coordinates, same value as the `HingeJointDef.m_hinge_axis_obj1` conversion)
- [x] 1.2 Fill `axis` in `engine/Framework/Import/UrdfLoader.cpp` at the revolute/continuous joint branch (`UrdfAxisToEngine(joint.axis)`, alongside `UrdfLoader.cpp:489`); leave default for fixed/unrealized joints

## 2. Actuator base class and specializations

- [x] 2.1 Create `app/physics/Actuator.h` (and `.cpp`): `Actuator` base class with virtual `float ComputeTorque(float angle, float angular_velocity) const = 0`, non-virtual `SetTargetAngle`/`GetTargetAngle`, `m_target_angle` member, virtual destructor
- [x] 2.2 Add `PdActuator` (kp=25, kd=0.5 defaults) with the PD law and angle-error wrap to `[âˆ’Ï€, Ï€)` (internal `clamp_angle` helper, file-local)
- [x] 2.3 Add `DcMotorActuator` (kp=25, kd=0.5, stall=33.5, no_load=21.0, cont=13.4, gear=1.0 defaults) with the four-quadrant torque-speed envelope clip (precompute `m_corner_speed` in the constructor)
- [x] 2.4 Verify no framework includes in `Actuator.h` (glm/std only), matching `PhysicsApp.h` conventions

## 3. PhysicsApp joint identity and registry

- [x] 3.1 Add `using JointId = uint32_t;` and `INVALID_JOINT_ID` to `app/physics/PhysicsApp.h`; add `JointState { angle, angular_velocity }` struct
- [x] 3.2 Add `JointId id{}` to `JointBodyPair`; document that it is assigned by the app and valid across both phases
- [x] 3.3 In `PhysicsApp::Impl`, add `struct JointRecord { std::string name; BodyId parent; BodyId child; glm::vec3 axis; glm::quat initial_rel_rotation; std::optional<glm::vec4> limits; }` (limits = lower/upper/effort/velocity) and `std::vector<JointRecord> m_joints;`
- [x] 3.4 Change `AddHingeJoint` signature `void â†?JointId`; in the implementation, create the joint (via SceneBuilder), append a `JointRecord` (axis = normalized `params.axis_obj1`), return the index; update any existing callers that rely on the void signature (they still compile â€?verify)
- [x] 3.5 In `LoadUrdf`: assign `JointId` per realized joint, store axis from `UrdfBuiltJoint::axis`, store name and URDF limits (from `robot.joints`), and fill `JointBodyPair::id` in `UrdfImportResult`
- [x] 3.6 At `CommitScene` (after `FlushCmdQueue`, before the scene freeze completes): compute `initial_rel_rotation = inverse(q_parent) * q_child` per record from GO world transforms via `SceneBuilder::GetBodyGameObject`; verify world transforms are final at this point
- [x] 3.7 Add a public way to read recorded URDF limits for a joint (e.g. `GetJointLimits(JointId)`) per the "limits recorded but not enforced" spec requirement

## 4. Actuator registration, targets, and joint state API

- [x] 4.1 Declare `void AddActuator(JointId joint, std::unique_ptr<Actuator> actuator);` in `PhysicsApp.h`; include `Actuator.h` there
- [x] 4.2 Implement `AddActuator` with the error contract: Building-phase-only (`std::logic_error` after commit), invalid `JointId` â†?`std::out_of_range`, null actuator â†?`std::invalid_argument`, duplicate joint â†?`std::invalid_argument`; store as `std::vector<std::unique_ptr<Actuator>> m_actuators` indexed by `JointId`
- [x] 4.3 Implement `SetTargetAngle(JointId, float)`: both phases legal, invalid `JointId` â†?`std::out_of_range`, no actuator â†?`std::logic_error`; delegates to `Actuator::SetTargetAngle`
- [x] 4.4 Implement `GetJointState(JointId) const`: Drive-phase-only (`std::logic_error` before commit), invalid `JointId` â†?`std::out_of_range`; compute q and Ï‰ per design D3 from the CPU body-state staging, `JointRecord` axis and `initial_rel_rotation`

## 5. Step integration

- [x] 5.1 In `Step()`, after the previous step's body states are available and before `RecordBodyStateUpload`, run actuator pass: for each actuator, read joint state, compute `Ï„`, and write `ExternalTorque` (+Ï„Â·axis_world to child, âˆ’Ï„Â·axis_world to parent) through the existing dirty-flag upload path
- [x] 5.2 Ensure actuator-written torques and user `SetBodyValue` writes coexist correctly in the upload path (actuator torques always overwrite per step; document the ownership warning in the header)

## 6. Tests

- [x] 6.1 Create `test/app/physics/physics_app_actuator_unit_test.cpp`: numeric tests for `PdActuator` (law value, angle wrap) and `DcMotorActuator` (envelope edges: zero-speed stall clip, high-speed sign reversal, Â±cont_torque bounds); no physics scene required
- [x] 6.2 Create `test/app/physics/physics_app_actuator_stand_test.cpp`: windowed, ground plane + A1 + 12 `DcMotorActuator`s with `DEFAULT_JOINT_POS` targets; all kp/kd/motor constants and targets at the top of the file; periodic `SDL_LogInfo` (every K steps: joint name / target / current angle / error); follows the `physics_app_windowed_test.cpp` loop pattern (finite-frame argv mode for ctest, paused-by-default manual mode)
- [x] 6.3 Register both tests in `test/app/physics/CMakeLists.txt` (`anro_add_test` with `WINDOWED FRAMES 600 TIMEOUT 300` for the standing test)
- [x] 6.4 Build and run the unit test plus the standing smoke test; confirm no crashes and that the tuning log prints plausible angles

## 7. Documentation

- [x] 7.1 Update `PhysicsApp.h` class docs: actuator section (registration, targets, readback, torque ownership warning, kinematic-joint note, limits-not-enforced note)
- [x] 7.2 Update `docs/build_instructions` or relevant app docs if the new tests need any mention
