cmake_minimum_required(VERSION 3.22.1)

project("updatecheck")

add_library(
        updatecheck
        SHARED
        native_updater.cpp
)

find_library(log-lib log)

target_link_libraries(
        updatecheck
        ${log-lib}
)
