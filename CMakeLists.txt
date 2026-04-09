cmake_minimum_required(VERSION 3.14)
project(LogosPackageManagerModulePlugin LANGUAGES CXX)

# Include the Logos Module CMake helper
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# LogosProviderBase / LOGOS_METHOD — dispatch is generated under generated_code/
logos_module(
    NAME package_manager
    SOURCES
        src/package_manager_impl.h
        src/package_manager_impl.cpp
        src/package_manager_loader.h
    EXTERNAL_LIBS
        package_manager_lib
        lgx
)
