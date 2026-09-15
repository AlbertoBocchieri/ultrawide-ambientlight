# CMakeList.txt : CMake project for ambientlight, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.14)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("ambientlight")


add_compile_definitions(UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)

# compile shaders
find_program(FXC fxc DOC "fx shader compiler")
if ("${FXC}" STREQUAL "FXC-NOTFOUND")
    message(STATUS "FXC not found; using the bundled shader compiler")
    add_executable(shader_compiler EXCLUDE_FROM_ALL tools/shader_compiler.cpp)
    target_link_libraries(shader_compiler PRIVATE d3dcompiler)
else()
	message(STATUS "Found FXC: ${FXC}")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")

# Define the function to handle individual shader compilation
function(compile_shader_entry SHADER_FILE ENTRY_POINT VARIABLE_SUFFIX DEFINES)
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    
    # Create a unique name for the file and the C++ variable
    set(UNIQUE_NAME "${SHADER_NAME}_${VARIABLE_SUFFIX}")
    set(SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${UNIQUE_NAME}_bin.h")

    if (FXC)
        set(PREPROCESSOR_ARGS "")
        foreach(DEF ${DEFINES})
            list(APPEND PREPROCESSOR_ARGS "/D${DEF}")
        endforeach()
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${FXC} /T cs_5_0 /E ${ENTRY_POINT} ${PREPROCESSOR_ARGS} /Fh ${SHADER_OUTPUT} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE} /Vn g_${UNIQUE_NAME}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
            COMMENT "Compiling ${SHADER_FILE} [Entry: ${ENTRY_POINT}] [Defines: ${DEFINES}] -> g_${UNIQUE_NAME}"
        )
    else()
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND shader_compiler ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE} ${ENTRY_POINT} ${SHADER_OUTPUT} g_${UNIQUE_NAME} ${DEFINES}
            DEPENDS shader_compiler ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
            COMMENT "Compiling ${SHADER_FILE} [Entry: ${ENTRY_POINT}] [Defines: ${DEFINES}] -> g_${UNIQUE_NAME}"
        )
    endif()

    # Export the generated header path to the global list
    set(GENERATED_SHADER_HEADERS ${GENERATED_SHADER_HEADERS} ${SHADER_OUTPUT} PARENT_SCOPE)
endfunction()
compile_shader_entry("shaders/blur.hlsl" "main" "main" "")
compile_shader_entry("shaders/luma.hlsl" "mainSDR" "mainSDR" "")
compile_shader_entry("shaders/luma.hlsl" "mainHDR10" "mainHDR10" "")
compile_shader_entry("shaders/luma.hlsl" "mainSCRGB" "mainSCRGB" "")
compile_shader_entry("shaders/luma.hlsl" "CSRowAnalysis" "CSRowAnalysis" "")
compile_shader_entry("shaders/luma.hlsl" "CSColAnalysis" "CSColAnalysis" "")
compile_shader_entry("shaders/copy.hlsl" "main" "main" "")
compile_shader_entry("shaders/vignette.hlsl" "main" "main" "")
compile_shader_entry("shaders/vignette.hlsl" "main" "mainUNorm" "USE_UNORM")
compile_shader_entry("shaders/mask.hlsl" "main" "main" "")
compile_shader_entry("shaders/mask.hlsl" "main" "mainUNorm" "USE_UNORM")

include_directories(${CMAKE_CURRENT_BINARY_DIR}/shaders)

include(FetchContent)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp")
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
else()
    message(STATUS "ImGui submodule is missing; fetching the pinned revision")
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 2737dbb1e1cd206fc874a5ca17ee672a68523182
        GIT_SHALLOW FALSE
    )
    FetchContent_GetProperties(imgui)
    if (NOT imgui_POPULATED)
        FetchContent_Populate(imgui)
    endif()
    set(IMGUI_DIR "${imgui_SOURCE_DIR}")
endif()

include(CheckIncludeFileCXX)
check_include_file_cxx(DirectXPackedVector.h HAVE_DIRECTXMATH)
if (NOT HAVE_DIRECTXMATH)
    message(STATUS "DirectXMath headers are missing; fetching them")
    FetchContent_Declare(directxmath
        GIT_REPOSITORY https://github.com/microsoft/DirectXMath.git
        GIT_TAG 93e6399d6d15e1e57f80b5cea04d5a8eb892d693
    )
    FetchContent_GetProperties(directxmath)
    if (NOT directxmath_POPULATED)
        FetchContent_Populate(directxmath)
    endif()
    set(DIRECTXMATH_INCLUDE_DIR "${directxmath_SOURCE_DIR}/Inc")
endif()

set(SRC
	app.cpp
	ambientlight.cpp
	capture.cpp
	present.cpp
	settings.cpp
	ui.cpp
	shaders/copy.cpp
	shaders/blur.cpp
	shaders/vignette.cpp
	shaders/fullscreenquad.cpp
	shaders/detect.cpp
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
	${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
	${IMGUI_DIR}/backends/imgui_impl_win32.cpp
	resources.rc resources.h
)

# Add source to this project's executable.
add_executable (ambientlight WIN32 ${SRC} ${GENERATED_SHADER_HEADERS})
target_include_directories(ambientlight PRIVATE "${IMGUI_DIR}" "${DIRECTXMATH_INCLUDE_DIR}")

# Additional target that's compiled with uiaccess
add_executable (ambientlight_a WIN32 ${SRC} ${GENERATED_SHADER_HEADERS})
target_include_directories(ambientlight_a PRIVATE "${IMGUI_DIR}" "${DIRECTXMATH_INCLUDE_DIR}")
if (MSVC)
    target_link_options(ambientlight_a PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='true'")
elseif (MINGW)
    target_sources(ambientlight_a PRIVATE uiaccess.rc)
endif()

foreach(TARGET ambientlight ambientlight_a)
    target_link_libraries(${TARGET} PRIVATE d3d11 d3dcompiler dxgi dcomp winmm dwmapi)
    if (MINGW)
        target_link_options(${TARGET} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endforeach()


if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ambientlight PROPERTY CXX_STANDARD 20)
  set_property(TARGET ambientlight_a PROPERTY CXX_STANDARD 20)
endif()

install(TARGETS ambientlight DESTINATION bin)
install(TARGETS ambientlight_a DESTINATION bin)
# copy files from scripts folder to bin
install(DIRECTORY scripts/ DESTINATION bin)
install(FILES config.ini DESTINATION bin)
