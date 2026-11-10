anro_add_test(physics_app_physics_only_test LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
anro_add_test(physics_app_offscreen_test LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
# The background test creates exactly one app per process (MainClass is a
# process singleton), so the second scenario reuses the same executable with the
# configured color as arguments instead of a second executable.
anro_add_test(physics_app_background_test LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
add_test(
    NAME physics_app_background_configured_test
    COMMAND
        "${ENGINE_PRODUCT_DIR}/bin/physics_app_background_test${CMAKE_EXECUTABLE_SUFFIX}" 0.25 0.5 0.75
)
set_tests_properties(physics_app_background_configured_test PROPERTIES
    LABELS "headless"
    ENVIRONMENT "NEEDRENAME_DATA_ROOT="
)
anro_add_test(physics_app_windowed_test WINDOWED FRAMES 120 TIMEOUT 300 LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
anro_add_test(physics_app_actuator_unit_test LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
anro_add_test(physics_app_joint_torque_test LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
anro_add_test(physics_app_reset_test LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
anro_add_test(physics_app_friction_test LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
# 2000 steps of a 12-joint robot: a backend that has to map a read-back buffer to
# make a step's result host-visible pays a map-and-copy per step, so the cap is
# generous on purpose. Vulkan needs ~1 minute; WebGPU needs several.
anro_add_test(physics_app_actuator_stand_test FRAMES 2000 TIMEOUT 900 LINK "Engine;PhysicsApp" FOLDER tests/app/physics)

# Long-run stability probe for the XPBD GPU solver: the 6-layer brick pyramid must
# survive a 60-step headless run without any body's velocity jumping by orders of
# magnitude.  It is the guard the radix sort's stability contract exists for — the
# pre-change (unstable) sort blows the scene up within a few steps.
anro_add_test(physics_app_stability_test ARGS 60 6 3.0 TIMEOUT 300
    LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
