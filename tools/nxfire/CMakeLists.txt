cmake_minimum_required(VERSION 3.16)

add_executable(nxfire-cli main.cpp)
set_target_properties(nxfire-cli PROPERTIES OUTPUT_NAME "nxfire")
target_link_libraries(nxfire-cli PRIVATE nxfire)
target_compile_definitions(nxfire-cli PRIVATE NXFIRE_VERSION="${PROJECT_VERSION}")

if(WIN32)
    target_link_libraries(nxfire-cli PRIVATE winhttp ws2_32)
endif()

if(WIN32 AND NXFIRE_BUILD_SHARED)
    add_custom_target(nxfire-cli-dll ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:nxfire>
            $<TARGET_FILE_DIR:nxfire-cli>
        DEPENDS nxfire)
endif()

# Salin server/klien/watcher ke folder nxfire agar `nxfire start` menemukannya
# saat dijalankan dari folder build (tanpa perlu memasang dulu).
foreach(_t nxfire-server nxfire-client nxfire-watch)
    if(TARGET ${_t})
        add_custom_command(TARGET nxfire-cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${_t}>
                $<TARGET_FILE_DIR:nxfire-cli>)
    endif()
endforeach()

install(TARGETS nxfire-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
