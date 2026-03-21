cmake_minimum_required(VERSION 3.14)
project(traffic_violation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cross-compile toolchain is set via -DCMAKE_TOOLCHAIN_FILE=toolchain/runtime.cmake

# ── Build options ─────────────────────────────────────────────────────────────
option(WITH_DRP   "Build with DRP-AI TVM runtime" ON)

# ── Find packages ─────────────────────────────────────────────────────────────
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
)

message(STATUS "GStreamer version : ${GST_VERSION}")
message(STATUS "GStreamer CFLAGS  : ${GST_CFLAGS}")
message(STATUS "GStreamer LDFLAGS : ${GST_LDFLAGS}")

find_package(Threads REQUIRED)

# yaml-cpp (header-only fallback included in 3rdparty, or system lib)
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    message(STATUS "yaml-cpp not found via find_package – using bundled mini-parser")
    set(USE_BUNDLED_YAML ON)
endif()

# ── DRP-AI TVM runtime paths ───────────────────────────────────────────────────
if(WITH_DRP)
    # TVM_HOME được set sẵn trong Docker container ($TVM_HOME = /drp-ai_tvm/tvm)
    if(DEFINED ENV{TVM_HOME})
        set(TVM_ROOT $ENV{TVM_HOME})
    else()
        set(TVM_ROOT ${CMAKE_SOURCE_DIR}/../../tvm)
    endif()
    set(RUNTIME_SRCS
        src/drp/MeraDrpRuntimeWrapper.cpp   
        src/drp/PreRuntime.cpp               
        src/drp/wayland.cpp
        src/drp/dmabuf.cpp
        src/drp/image.cpp
    )
    include_directories(
        ${TVM_ROOT}/include
        ${TVM_ROOT}/3rdparty/dlpack/include
        ${TVM_ROOT}/3rdparty/dmlc-core/include
        ${TVM_ROOT}/3rdparty/compiler-rt          
        ${CMAKE_SOURCE_DIR}/src/drp
    )
    add_definitions(-DWITH_DRP)
endif()

# ── Tesseract OCR ─────────────────────────────────────────────────────────────
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
    src/streaming/StreamPipeline.cpp
)

if(WITH_DRP)
    list(APPEND SOURCES ${RUNTIME_SRCS})
    # lib64 chứa libmmngr, libmmngrbuf trên Renesas sysroot
    link_directories($ENV{SDK}/sysroots/aarch64-poky-linux/usr/lib64)
endif()

# ── Include dirs ──────────────────────────────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}      # ← GStreamer headers
    $ENV{SDK}/sysroots/aarch64-poky-linux/usr/lib64/glib-2.0/include  # ← glibconfig.h
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
    ${GST_LIBRARIES}         # ← GStreamer libs
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
        mmngr
        mmngrbuf
    )
endif()

target_link_libraries(traffic_violation PRIVATE ${TESSERACT_LIBRARIES})

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS traffic_violation DESTINATION bin)
install(FILES config.yaml DESTINATION bin)