cmake_minimum_required(VERSION 3.16)

add_executable(nxfire-watch main.cpp)

if(WIN32)
    target_link_libraries(nxfire-watch PRIVATE winhttp ws2_32)
endif()

install(TARGETS nxfire-watch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
