cmake_minimum_required(VERSION 3.16)

add_library(jetcamd_config STATIC
    src/config.cpp
    src/pipeline.cpp
)
target_include_directories(jetcamd_config PUBLIC include)
target_compile_features(jetcamd_config PUBLIC cxx_std_17)
target_compile_options(jetcamd_config PRIVATE -Wall -Wextra -Wpedantic)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET IMPORTED_TARGET
        gstreamer-1.0
        gstreamer-app-1.0
    )
    pkg_check_modules(GSTREAMER_RTSP QUIET IMPORTED_TARGET
        gstreamer-rtsp-server-1.0
    )
endif()

if(GSTREAMER_FOUND)
    add_executable(jetcamd src/main.cpp)
    target_link_libraries(jetcamd PRIVATE jetcamd_config PkgConfig::GSTREAMER)
    target_compile_features(jetcamd PRIVATE cxx_std_17)
    target_compile_options(jetcamd PRIVATE -Wall -Wextra -Wpedantic)

    if(GSTREAMER_RTSP_FOUND)
        target_sources(jetcamd PRIVATE src/rtsp_server.cpp)
        target_link_libraries(jetcamd PRIVATE PkgConfig::GSTREAMER_RTSP)
        target_compile_definitions(jetcamd PRIVATE JETCAM_HAVE_RTSP_SERVER=1)
    endif()

    install(TARGETS jetcamd RUNTIME DESTINATION bin)
else()
    message(STATUS "GStreamer development files not found; jetcamd executable will not be built")
endif()

if(BUILD_TESTING)
    add_executable(jetcamd-config-test tests/config_test.cpp)
    target_link_libraries(jetcamd-config-test PRIVATE jetcamd_config)
    add_test(NAME jetcamd-config-test COMMAND jetcamd-config-test)
endif()
