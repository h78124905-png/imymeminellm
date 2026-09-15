cmake_minimum_required(VERSION 3.22.1)
project(agentllm)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP ON CACHE BOOL "" FORCE)
set(GGML_CPU_ARM_ARCH "armv8.6-a+i8mm+dotprod+fp16" CACHE STRING "" FORCE)
add_subdirectory(llama.cpp)
add_library(agentllm SHARED jni_bridge.cpp)
target_link_libraries(agentllm
    common
    llama
    ggml
    android
    log
)
target_include_directories(agentllm PRIVATE llama.cpp/common llama.cpp/include)
