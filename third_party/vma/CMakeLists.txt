project(VulkanMemoryAllocator)

find_package(vulkan REQUIRED)

add_library(vma STATIC vk_mem_alloc.cpp)
target_link_libraries(vma Vulkan::Vulkan)
target_include_directories(vma SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
