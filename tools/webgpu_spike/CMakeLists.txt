# Spike harness for the WebGPU backend (task group 1 of `rhi-webgpu-backend`).
#
# One source, two shapes:
#   * desktop: a headless executable linked against a WebGPU implementation
#     (wgpu-native or Dawn) found through WEBGPU_ROOT — the validation and A/B
#     environment;
#   * browser: an Emscripten page that renders into a canvas — the product
#     environment.
#
# It is opt-in (ENGINE_BUILD_WEBGPU_SPIKE) because it is a measurement harness,
# not a product target, and it deliberately depends on nothing else in the
# engine: it is the smallest thing that proves the standard WebGPU C interface
# works in both shapes.

if (NOT ENGINE_RHI_BACKEND_WEBGPU)
    message(FATAL_ERROR "tools/webgpu_spike requires ENGINE_RHI_BACKEND=webgpu.")
endif()

add_executable(webgpu_spike main.cpp)
target_link_libraries(webgpu_spike PRIVATE EngineDepWebgpu)
set_target_properties(webgpu_spike PROPERTIES FOLDER tools)

if (EMSCRIPTEN)
    # A browser page: the module, its JavaScript glue and an HTML shell that
    # owns the canvas the sample renders into.
    set_target_properties(webgpu_spike PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "-sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576"
    )
    target_link_options(webgpu_spike PRIVATE
        "SHELL:-sASYNCIFY=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sENVIRONMENT=web"
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    )
else()
    # The desktop shape is a console program; wgpu-native ships its DLL next to
    # the import library, so the runtime copy is what makes it runnable in place.
    if (WebGPU_LIBRARY AND EXISTS "${WebGPU_LIBRARY}")
        get_filename_component(_webgpu_library_dir "${WebGPU_LIBRARY}" DIRECTORY)
        foreach(_candidate wgpu_native.dll webgpu_dawn.dll)
            if (EXISTS "${_webgpu_library_dir}/${_candidate}")
                add_custom_command(
                    TARGET webgpu_spike POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_webgpu_library_dir}/${_candidate}"
                        "$<TARGET_FILE_DIR:webgpu_spike>/${_candidate}"
                )
            endif()
        endforeach()
    endif()
endif()
