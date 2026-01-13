set(TARGET_NAME shader_compile_test)

add_executable(${TARGET_NAME} shader_compile_test.cpp)
target_link_libraries(${TARGET_NAME} PUBLIC engine)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER engine_tests)
