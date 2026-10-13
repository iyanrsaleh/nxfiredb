cmake_minimum_required(VERSION 3.16)

project(nxfire VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(NXFIRE_BUILD_SHARED "Build nxfire as a shared library" OFF)
option(NXFIRE_BUILD_SERVER "Build nxfire-server, nxfire-client, and tools" ON)

# --- core library ----------------------------------------------------------
if(NXFIRE_BUILD_SHARED)
    add_library(nxfire SHARED)
else()
    add_library(nxfire STATIC)
endif()

target_sources(nxfire PRIVATE
    src/Nxfire.cpp
    src/Json.cpp
    src/Http.cpp
)

target_include_directories(nxfire
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(nxfire PRIVATE NXFIRE_LIBRARY)

if(NXFIRE_BUILD_SHARED)
    set_target_properties(nxfire PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    target_compile_definitions(nxfire PUBLIC NXFIRE_STATIC)
endif()

find_package(Threads REQUIRED)
target_link_libraries(nxfire PRIVATE Threads::Threads)

if(WIN32)
    # WinHTTP: HTTP client bawaan Windows (tanpa dependensi eksternal)
    target_link_libraries(nxfire PRIVATE winhttp ws2_32)
endif()

# --- install / export -------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS nxfire
    EXPORT nxfireTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT nxfireTargets
    FILE nxfireTargets.cmake
    NAMESPACE nxfire::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nxfire
)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/nxfireConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/nxfireConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nxfire
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/nxfireConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/nxfireConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/nxfireConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nxfire
)

# --- server + client + tools -----------------------------------------------
if(NXFIRE_BUILD_SERVER)
    add_subdirectory(server)
    add_subdirectory(client)
    add_subdirectory(tools/nxfire)
    add_subdirectory(tools/watch)
endif()
