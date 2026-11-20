cmake_minimum_required(VERSION 4.2)
project(CPUlator)

set(CMAKE_CXX_STANDARD 20)

add_executable(CPUlator main.cpp
        SingleInstructorCPU.cpp)
