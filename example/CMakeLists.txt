macro(subdirlist result curdir)
    file(GLOB children RELATIVE ${curdir} ${curdir}/*)
    set(dirlist "")
    foreach(child ${children})
        if(IS_DIRECTORY ${curdir}/${child})
            list(APPEND dirlist ${child})
            # message(${child})
        endif()
    endforeach()
    set(${result} ${dirlist})
endmacro()

subdirlist(subdirs ${CMAKE_CURRENT_SOURCE_DIR})

# foreach(dir ${subdirs})
#     add_subdirectory(${dir})
# endforeach()
add_subdirectory(external_resource_loading_example)
# The editor example hosts the editor, which is a Vulkan application, so this is built only where
# both the apps and that backend are present.
if (ENGINE_BUILD_APPS AND ENGINE_RHI_BACKEND_VULKAN)
    add_subdirectory(editor_run_game_example)
endif()
# An example that presents through WebGPU directly: it exists to answer whether a surface can be
# acquired and presented at all, on either a canvas or a window, so it is built only where the
# WebGPU backend is the one selected.
if (ENGINE_RHI_BACKEND_WEBGPU)
    add_subdirectory(webgpu_clear_example)
endif()


