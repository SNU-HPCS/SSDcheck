cmake_minimum_required(VERSION 3.10)
project(usecase_object_storage)
set(CMAKE_CXX_STANDARD 14)

add_executable(usecase_object_storage
        main.cpp
        utils.cpp
        worker.cpp
        stat.cpp)

target_link_libraries(usecase_object_storage
        pthread)