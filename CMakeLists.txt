cmake_minimum_required(VERSION 3.20)
project(LegacyCraft LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Compiler definitions and options for GCC / Clang / MSVC
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -fms-extensions
        -Wno-non-c-typedef-for-linkage
        -Wno-invalid-offsetof
        -Wno-attributes
        -Wno-write-strings
        -Wno-narrowing
        -Wno-conversion-null
        -Wno-literal-suffix
        -Wno-return-type
        -Wno-unused-variable
    )
endif()

add_compile_definitions(
    _WINDOWS64
    __WINDOWS64
    _LARGE_WORLDS
    _DEBUG_MENUS_ENABLED
    _CRT_SECURE_NO_WARNINGS
    _CRT_NON_CONFORMING_SWPRINTFS
    WIN32_LEAN_AND_MEAN
)

# -------------------------------------------------------------
# 1. Minecraft.World (Static Library)
# -------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.World/sources.cmake)

add_library(Minecraft.World STATIC ${WORLD_SOURCES})

target_compile_definitions(Minecraft.World PRIVATE _LIB)

target_include_directories(Minecraft.World PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.World
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.World/x64headers
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Common
)

target_precompile_headers(Minecraft.World PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.World/stdafx.h"
)

# -------------------------------------------------------------
# 2. Modular Render Backend DLLs (DX11, OpenGL, Vulkan)
# -------------------------------------------------------------
set(RENDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Render")
set(IGGY_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Iggy/include")
set(IGGY_GDRAW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Iggy/gdraw")

add_library(C4J_Render_DX11 SHARED
    "${RENDER_DIR}/Backend_DX11.cpp"
    "${IGGY_GDRAW_DIR}/gdraw_d3d11.cpp"
)
target_include_directories(C4J_Render_DX11 PRIVATE
    "${RENDER_DIR}/dx11_gdraw_shim"
    "${RENDER_DIR}"
    "${IGGY_INC_DIR}"
    "${IGGY_GDRAW_DIR}"
)
target_compile_features(C4J_Render_DX11 PRIVATE cxx_std_17)
target_link_libraries(C4J_Render_DX11 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Iggy/lib/libiggy_w64.a"
    d3d11 dxgi d3dcompiler
)
target_link_options(C4J_Render_DX11 PRIVATE -static)
set_target_properties(C4J_Render_DX11 PROPERTIES PREFIX "" OUTPUT_NAME "C4J_Render_DX11")

add_library(C4J_Render_GL SHARED
    "${RENDER_DIR}/Backend_GL.cpp"
    "${IGGY_GDRAW_DIR}/gdraw_wgl.c"
)
target_include_directories(C4J_Render_GL PRIVATE
    "${RENDER_DIR}"
    "${IGGY_INC_DIR}"
    "${IGGY_GDRAW_DIR}"
)
target_compile_features(C4J_Render_GL PRIVATE cxx_std_17)
target_link_libraries(C4J_Render_GL PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Iggy/lib/libiggy_w64.a"
    opengl32 gdi32
)
target_link_options(C4J_Render_GL PRIVATE -static)
set_target_properties(C4J_Render_GL PROPERTIES PREFIX "" OUTPUT_NAME "C4J_Render_GL")

add_library(C4J_Render_Vulkan SHARED
    "${RENDER_DIR}/Backend_Vulkan.cpp"
    "${RENDER_DIR}/gdraw_vulkan.cpp"
)
target_include_directories(C4J_Render_Vulkan PRIVATE
    "${RENDER_DIR}"
    "${IGGY_INC_DIR}"
)
target_compile_features(C4J_Render_Vulkan PRIVATE cxx_std_17)
target_link_libraries(C4J_Render_Vulkan PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Iggy/lib/libiggy_w64.a"
    gdi32
)
target_link_options(C4J_Render_Vulkan PRIVATE -static)
set_target_properties(C4J_Render_Vulkan PROPERTIES PREFIX "" OUTPUT_NAME "C4J_Render_Vulkan")

add_library(C4J_Render_DX12 SHARED
    "${RENDER_DIR}/Backend_DX12.cpp"
    "${RENDER_DIR}/gdraw_dx12.cpp"
)
target_include_directories(C4J_Render_DX12 PRIVATE
    "${RENDER_DIR}"
    "${IGGY_INC_DIR}"
)
target_compile_features(C4J_Render_DX12 PRIVATE cxx_std_17)
target_link_libraries(C4J_Render_DX12 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Iggy/lib/libiggy_w64.a"
    d3d12 dxgi d3dcompiler gdi32
)
target_link_options(C4J_Render_DX12 PRIVATE -static)
set_target_properties(C4J_Render_DX12 PROPERTIES PREFIX "" OUTPUT_NAME "C4J_Render_DX12")

# -------------------------------------------------------------
# 3. Minecraft.Client (Executable)
# -------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/sources.cmake)

# Mark zlib C files to skip C++ precompiled header
file(GLOB ZLIB_C_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Common/zlib/*.c")
set_source_files_properties(${ZLIB_C_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

set(RC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/MinecraftWindows.rc")

add_executable(Minecraft.Client WIN32
    ${CLIENT_SOURCES}
    ${RC_FILE}
)

add_dependencies(Minecraft.Client
    C4J_Render_DX11
    C4J_Render_GL
    C4J_Render_Vulkan
    C4J_Render_DX12
)

target_include_directories(Minecraft.Client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Common
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Iggy/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Miles/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.World
    ${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.World/x64headers
)

target_precompile_headers(Minecraft.Client PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/stdafx.h"
)

target_link_libraries(Minecraft.Client PRIVATE
    Minecraft.World
    "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Iggy/lib/libiggy_w64.a"
    xinput
    dsound
    winmm
    ws2_32
    shlwapi
    ole32
    oleaut32
    uuid
    windowscodecs
    gdi32
    user32
    kernel32
)

target_link_options(Minecraft.Client PRIVATE
    -static
)

# Copy iggy_w64.dll and link Common assets to output folder next to exe
add_custom_command(TARGET Minecraft.Client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Minecraft.Client/Windows64/Iggy/lib/iggy_w64.dll"
        $<TARGET_FILE_DIR:Minecraft.Client>
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Minecraft.Client>/Common"
    COMMAND cmd /c if not exist "$<TARGET_FILE_DIR:Minecraft.Client>\\Common\\Media" mklink /J "$<TARGET_FILE_DIR:Minecraft.Client>\\Common\\Media" "${CMAKE_CURRENT_SOURCE_DIR}\\Minecraft.Client\\Common\\Media"
    COMMAND cmd /c if not exist "$<TARGET_FILE_DIR:Minecraft.Client>\\Common\\res" mklink /J "$<TARGET_FILE_DIR:Minecraft.Client>\\Common\\res" "${CMAKE_CURRENT_SOURCE_DIR}\\Minecraft.Client\\Common\\res"
)
