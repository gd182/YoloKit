cmake_minimum_required(VERSION 3.22.1)
project(yolokit)

set(CMAKE_CXX_STANDARD 17)

set(ncnn_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ncnn/${ANDROID_ABI}/lib/cmake/ncnn)
find_package(ncnn REQUIRED)

add_library(yolokit SHARED
    yolo.cpp
    yolo_jni.cpp
    qnn_yolo.cpp
    qnn_jni.cpp
)

if(QNN_SDK_ROOT AND ANDROID_ABI STREQUAL "arm64-v8a")
    if(EXISTS "${QNN_SDK_ROOT}/include/QNN/QnnInterface.h")
        target_compile_definitions(yolokit PRIVATE HAVE_QNN=1)
        target_include_directories(yolokit PRIVATE "${QNN_SDK_ROOT}/include/QNN")
        message(STATUS "QNN: enabled (${QNN_SDK_ROOT})")
    else()
        message(WARNING "QNN_SDK_ROOT=${QNN_SDK_ROOT} has no include/QNN/QnnInterface.h; QNN disabled")
    endif()
else()
    message(STATUS "QNN: disabled (${ANDROID_ABI}, QNN_SDK_ROOT='${QNN_SDK_ROOT}')")
endif()

target_compile_options(yolokit PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
target_compile_definitions(yolokit PRIVATE
    YOLOKIT_ENABLE_DIAGNOSTICS=$<IF:$<CONFIG:Debug>,1,0>
)
target_link_options(yolokit PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/yolokit.map"
    "-Wl,--no-undefined-version"
    "-Wl,-z,relro"
    "-Wl,-z,now"
)
set_property(TARGET yolokit APPEND PROPERTY
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/yolokit.map"
)

find_library(log-lib log)

target_link_libraries(yolokit
    ncnn
    jnigraphics
    android
    ${log-lib}
)
