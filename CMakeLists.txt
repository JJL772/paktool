cmake_minimum_required(VERSION 3.20)
project(paktool CXX)

add_executable(
    paktool

    src/main.cpp
)

target_include_directories(
	paktool PRIVATE thirdparty
)

include(GNUInstallDirs)
install(TARGETS paktool)

