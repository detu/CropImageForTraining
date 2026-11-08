cmake_minimum_required(VERSION 4.2)
project(ImageCropper)

set(CMAKE_CXX_STANDARD 20)
find_package(OpenCV REQUIRED)

add_executable(ImageCropper main.cpp)
target_link_libraries(ImageCropper PRIVATE ${OpenCV_LIBS})
