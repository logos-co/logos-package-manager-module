cmake_minimum_required(VERSION 3.14)
project(PackageManagerModuleTests LANGUAGES CXX)

include(LogosTest)

# Find the external libraries (copied to lib/ by preConfigure)
find_library(PM_LIB package_manager_lib PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../lib" NO_DEFAULT_PATH REQUIRED)
find_library(LGX_LIB lgx PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../lib" NO_DEFAULT_PATH REQUIRED)

logos_test(
    NAME package_manager_module_tests
    MODULE_SOURCES
        ../src/package_manager_impl.cpp
    TEST_SOURCES
        main.cpp
        test_directory_config.cpp
        test_scanning.cpp
        test_signature.cpp
        test_keyring.cpp
    EXTRA_INCLUDES
        ../lib
    GENERATED_SOURCES
        ../generated_code/logos_provider_dispatch.cpp
    GENERATED_DIR
        ../generated_code
    EXTRA_LINK_LIBS
        ${PM_LIB}
        ${LGX_LIB}
)
