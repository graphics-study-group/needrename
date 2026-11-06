if (NOT ENGINE_RHI_BACKEND_WEBGPU)
    return()
endif()

set(DAWN_ROOT "" CACHE PATH "Dawn prefix, or a directory containing matching debug/release prefixes")
set(SHADER_WGSL_CONVERTER "" CACHE FILEPATH "SPIR-V to WGSL converter (Tint)")
add_library(EngineDepWebgpu INTERFACE)

if (EMSCRIPTEN)
    # Browser builds need only native Tint from a manual Dawn prefix. They never
    # download or link the desktop Dawn library.
    set(_dawn_prefix "${DAWN_ROOT}")
    if (NOT _dawn_prefix)
        set(_dawn_prefix "$ENV{DAWN_ROOT}")
    endif()
    if (IS_DIRECTORY "${_dawn_prefix}/release")
        set(_dawn_prefix "${_dawn_prefix}/release")
    elseif (NOT _dawn_prefix AND Dawn_DIR)
        get_filename_component(_dawn_prefix "${Dawn_DIR}/../../.." ABSOLUTE)
    endif()
else()
    find_package(Threads REQUIRED)
    include(cmake/DawnPrebuilt.cmake)
    set(_dawn_prefix "${DAWN_TINT_PREFIX}")
    message(STATUS "Dawn package: ${Dawn_DIR}")
    message(STATUS "Dawn Debug library: ${DAWN_HAS_DEBUG}")
    # Dawn's exported target names Threads::Threads without finding it.
    target_link_libraries(EngineDepWebgpu INTERFACE dawn::webgpu_dawn Threads::Threads)
endif()

if (NOT SHADER_WGSL_CONVERTER)
    if (CMAKE_HOST_WIN32)
        set(_tint_name tint.exe)
    else()
        set(_tint_name tint)
    endif()
    if (_dawn_prefix AND EXISTS "${_dawn_prefix}/bin/${_tint_name}")
        set(SHADER_WGSL_CONVERTER "${_dawn_prefix}/bin/${_tint_name}" CACHE FILEPATH
            "SPIR-V to WGSL converter (Tint)" FORCE)
    endif()
endif()
if (NOT SHADER_WGSL_CONVERTER)
    message(FATAL_ERROR
        "No SPIR-V to WGSL converter was found. Set DAWN_ROOT to a Dawn prefix containing "
        "bin/tint, or set SHADER_WGSL_CONVERTER to the Tint executable explicitly.")
endif()
message(STATUS "WGSL converter: ${SHADER_WGSL_CONVERTER}")

if (EMSCRIPTEN)
    add_library(WebGPU::WebGPU INTERFACE IMPORTED)
    set_target_properties(WebGPU::WebGPU PROPERTIES
        INTERFACE_COMPILE_OPTIONS "--use-port=emdawnwebgpu"
        INTERFACE_LINK_OPTIONS "--use-port=emdawnwebgpu"
    )
    target_link_libraries(EngineDepWebgpu INTERFACE WebGPU::WebGPU)
    return()
endif()

if (WIN32)
    # Dawn's D3D12 backend loads DXC dynamically. FXC remains in System32 and is
    # located by the runtime code.
    set(DXC_ROOT "" CACHE PATH "Extracted DirectXShaderCompiler release (contains bin/x64)")
    if (NOT DXC_ROOT AND DEFINED ENV{DXC_ROOT})
        set(DXC_ROOT "$ENV{DXC_ROOT}" CACHE PATH
            "Extracted DirectXShaderCompiler release (contains bin/x64)" FORCE)
    endif()
    set(_dxc_runtime_dir "${DXC_ROOT}/bin/x64")
    set(DXC_RUNTIME_FILES)
    set(_dxc_runtime_missing)
    foreach(_name dxcompiler.dll dxil.dll)
        if (EXISTS "${_dxc_runtime_dir}/${_name}")
            list(APPEND DXC_RUNTIME_FILES "${_dxc_runtime_dir}/${_name}")
        else()
            list(APPEND _dxc_runtime_missing "${_name}")
        endif()
    endforeach()
    if (_dxc_runtime_missing)
        list(JOIN _dxc_runtime_missing ", " _dxc_runtime_missing_text)
        message(FATAL_ERROR
            "DXC_ROOT '${DXC_ROOT}' is missing bin/x64 files: ${_dxc_runtime_missing_text}. "
            "Extract an official DirectXShaderCompiler release and set DXC_ROOT to its root directory.")
    endif()
    message(STATUS "DXC runtime: ${_dxc_runtime_dir}")

    add_custom_target(dawn_dxc_stage
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${DXC_RUNTIME_FILES} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
        COMMENT "Staging Dawn's DXC runtime DLLs"
        VERBATIM
    )
    set_target_properties(dawn_dxc_stage PROPERTIES FOLDER build)
    add_dependencies(EngineDepWebgpu dawn_dxc_stage)
    install(FILES ${DXC_RUNTIME_FILES} DESTINATION bin COMPONENT runtime)
endif()
