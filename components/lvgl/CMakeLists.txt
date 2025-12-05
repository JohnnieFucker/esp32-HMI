# LVGL Component CMakeLists.txt

# Collect all source files
file(GLOB_RECURSE LVGL_ASRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.S"
)

file(GLOB_RECURSE LVGL_CSRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

file(GLOB_RECURSE LVGL_DEMO_CSRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/demos/*.c"
)

file(GLOB_RECURSE LVGL_EXAMPLE_CSRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.c"
)

file(GLOB_RECURSE LVGL_CXXSRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# Combine all C/C++ sources
set(LVGL_ALL_SRCS 
    ${LVGL_CSRCS}
    ${LVGL_DEMO_CSRCS}
    ${LVGL_EXAMPLE_CSRCS}
    ${LVGL_CXXSRCS}
)

# Register the component
# Only include ASM_SRCS if there are any assembly files
if(LVGL_ASRCS)
    idf_component_register(
        SRCS ${LVGL_ALL_SRCS}
        ASM_SRCS ${LVGL_ASRCS}
        INCLUDE_DIRS 
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
else()
    idf_component_register(
        SRCS ${LVGL_ALL_SRCS}
        INCLUDE_DIRS 
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
endif()

