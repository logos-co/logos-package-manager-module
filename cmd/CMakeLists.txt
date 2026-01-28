# CLI executable for logos-package-manager
cmake_minimum_required(VERSION 3.14)

# CLI sources
set(CLI_SOURCES
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/lib/package_manager_lib.cpp
    ${CMAKE_SOURCE_DIR}/src/lib/package_manager_lib.h
)

# Create the CLI executable
add_executable(lgpm ${CLI_SOURCES})

# Include directories
target_include_directories(lgpm PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib
    ${LGX_ROOT}/include
)

# Link Qt libraries
target_link_libraries(lgpm PRIVATE 
    Qt${QT_VERSION_MAJOR}::Core 
    Qt${QT_VERSION_MAJOR}::Network
)

# Link lgx library
target_link_libraries(lgpm PRIVATE ${LGX_LIB})

# Set output directory
set_target_properties(lgpm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Platform-specific RPATH settings
if(APPLE)
    set_target_properties(lgpm PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/../lib;${LGX_ROOT}/lib"
    )
elseif(UNIX)
    set_target_properties(lgpm PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/../lib:${LGX_ROOT}/lib"
    )
endif()

# Install rules
install(TARGETS lgpm
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
