cmake_minimum_required(VERSION 3.21)
project(NewTsim C)

set(CMAKE_C_STANDARD 90)

add_executable(NewTsim master.c shared/so_conf.c shared/so_conf.h node.c node.h shared/so_ipc.c shared/so_ipc.h shared/so_random.c shared/so_random.h user.c user.h)
