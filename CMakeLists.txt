cmake_minimum_required(VERSION 3.16)

project(rk-r87pro-openrgb-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)

pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)

add_library(rk-r87pro MODULE
    src/R87ProPlugin.cpp
    src/R87ProPlugin.h
    src/R87ProDevice.cpp
    src/R87ProDevice.h
    src/R87ProDongle.cpp
    src/R87ProDongle.h
    src/R87ProLayout.cpp
    src/R87ProLayout.h
    src/metadata.json
)

target_include_directories(rk-r87pro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rk-r87pro PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::HIDAPI
)

target_compile_options(rk-r87pro PRIVATE -Wall -Wextra)

set_target_properties(rk-r87pro PROPERTIES
    PREFIX ""
    OUTPUT_NAME "rk-r87pro"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
