anro_add_test(physics_app_physics_only_test LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
anro_add_test(physics_app_offscreen_test LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
anro_add_test(physics_app_windowed_test WINDOWED FRAMES 120 TIMEOUT 300 LINK "Engine;PhysicsApp" FOLDER tests/app/physics)
