cmake_minimum_required(VERSION 3.14)
project(traffic_violation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cross-compile toolchain is set via -DCMAKE_TOOLCHAIN_FILE=toolchain/runtime.cmake

# ── Build options ─────────────────────────────────────────────────────────────
option(WITH_DRP   "Build with DRP-AI TVM runtime" ON)
# ── Find packages ─────────────────────────────────────────────────────────────
find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# yaml-cpp (header-only fallback included in 3rdparty, or system lib)
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    message(STATUS "yaml-cpp not found via find_package – using bundled mini-parser")
    set(USE_BUNDLED_YAML ON)
endif()

# ── DRP-AI TVM runtime paths ───────────────────────────────────────────────────
if(WITH_DRP)
    set(TVM_ROOT ${CMAKE_SOURCE_DIR}/../../tvm)
    set(RUNTIME_SRCS
        src/drp/MeraDrpRuntimeWrapper.cpp   # copied from /drp-ai_tvm/apps/
        src/drp/PreRuntime.cpp               # copied from /drp-ai_tvm/apps/
        src/drp/wayland.cpp
        src/drp/dmabuf.cpp
        src/drp/image.cpp
    )
    include_directories(
        ${TVM_ROOT}/include
        ${TVM_ROOT}/3rdparty/dlpack/include
        ${TVM_ROOT}/3rdparty/dmlc-core/include
        ${CMAKE_SOURCE_DIR}/src/drp
    )
    add_definitions(-DWITH_DRP)
endif()

# ── Tesseract OCR ─────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(TESSERACT REQUIRED tesseract)
include_directories(${TESSERACT_INCLUDE_DIRS})

# ── Source files ──────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/VideoInput.cpp
    src/common/config.cpp
    src/models/box.cpp
    src/models/TrafficDetector.cpp
    src/common/tess_module/TesseractEngine.cpp
    src/common/lp_text_proc_module/LpTextProc.cpp
    src/common/lp_regex_module/lp_regex_function.cpp
    src/common/lp_validate_module/LpValidate.cpp
    src/tracking/VehicleTracker.cpp
    src/violation/ViolationEngine.cpp
    src/violation/HelmetRule.cpp
    src/violation/RedLightRule.cpp
    src/violation/WrongLaneRule.cpp
    src/violation/LaneLineRule.cpp
    src/output/ViolationLogger.cpp
)

if(WITH_DRP)
    list(APPEND SOURCES ${RUNTIME_SRCS})
endif()

# ── Include dirs ──────────────────────────────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

if(yaml-cpp_FOUND)
    include_directories(${YAML_CPP_INCLUDE_DIR})
endif()

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(traffic_violation ${SOURCES})

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(traffic_violation
    PRIVATE
    ${OpenCV_LIBS}
    Threads::Threads
    dl
)

if(yaml-cpp_FOUND)
    target_link_libraries(traffic_violation PRIVATE ${YAML_CPP_LIBRARIES})
endif()

if(WITH_DRP)
    target_link_libraries(traffic_violation PRIVATE
        ${TVM_ROOT}/build_runtime/libtvm_runtime.so
        wayland-client
        EGL
        GLESv2
        wayland-egl
    )
endif()

target_link_libraries(traffic_violation PRIVATE ${TESSERACT_LIBRARIES})

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS traffic_violation DESTINATION bin)
install(FILES config.yaml DESTINATION bin)
