cmake_minimum_required(VERSION 3.18.2)

project(gpio_press_count)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

include_directories(libdill)
link_directories(libdill/.libs/)

add_compile_options(-Wall)
add_executable(gpio_press_count src/main.c)
target_link_libraries(gpio_press_count libdill.a)


