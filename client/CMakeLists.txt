cmake_minimum_required(VERSION 3.16)

add_executable(nxfire-client main.cpp)
target_link_libraries(nxfire-client PRIVATE nxfire)

if(WIN32)
    target_link_libraries(nxfire-client PRIVATE winhttp ws2_32)
endif()

if(WIN32 AND NXFIRE_BUILD_SHARED)
    add_custom_target(nxfire-client-dll ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:nxfire>
            $<TARGET_FILE_DIR:nxfire-client>
        DEPENDS nxfire)
endif()

install(TARGETS nxfire-client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
