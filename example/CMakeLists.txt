function(engine_configure_browser_example target)
    if (NOT EMSCRIPTEN)
        return()
    endif()

    set_target_properties(${target} PROPERTIES SUFFIX ".html")
    target_compile_options(${target} PRIVATE -fexceptions)
    target_link_options(${target} PRIVATE
        -fexceptions
        -sNO_DISABLE_EXCEPTION_CATCHING=1
        -sASYNCIFY
        -sASYNCIFY_STACK_SIZE=2097152
        "SHELL:--preload-file ${ASSETS_DIR}@/assets"
        "SHELL:--preload-file ${PROJECTS_DIR}@/projects"
        "SHELL:--preload-file ${ENGINE_DATA_STAGE_DIR}/builtin_assets@/builtin_assets"
        "SHELL:--preload-file ${ENGINE_DATA_STAGE_DIR}/spirv@/spirv"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=FS"
    )
    add_dependencies(${target} runtime_data_stage)
    target_compile_definitions(${target} PRIVATE ENGINE_DEV_DATA_ROOT_DIR="/")
endfunction()

add_subdirectory(external_resource_loading_example)
if (ENGINE_BUILD_APPS)
    add_subdirectory(physics_rigid_body_example)
    add_subdirectory(editor_run_game_example)
endif()
if (ENGINE_RHI_BACKEND_WEBGPU)
    add_subdirectory(webgpu_clear_example)
endif()
