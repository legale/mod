project(netlink_getlink C)
cmake_minimum_required(VERSION 3.18)
add_compile_options(-std=c11 -Wall)
set(CMAKE_C_STANDARD 11)

include_directories("/usr/include")

add_executable(getlink main.c libnl_getlink.c)