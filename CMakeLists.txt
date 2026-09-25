cmake_minimum_required(VERSION 3.20)

project(remote_gateway VERSION 0.1.0 LANGUAGES C CXX)

option(REMOTE_GATEWAY_BUILD_TESTS "Build unit tests" ON)
option(REMOTE_GATEWAY_ENABLE_STREAMING "Build WebRTC/H.264 streaming support" ON)

include(FetchContent)
find_package(Threads REQUIRED)

add_library(remote_gateway_core
    src/input_text.cpp
    src/security_policy.cpp
    src/session.cpp
    src/test_pattern_source.cpp
)
set_target_properties(remote_gateway_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(remote_gateway_core PUBLIC include)
target_compile_features(remote_gateway_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(remote_gateway_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(remote_gateway_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(REMOTE_GATEWAY_ENABLE_STREAMING)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPENH264 REQUIRED IMPORTED_TARGET openh264)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
    pkg_check_modules(FREERDP REQUIRED IMPORTED_TARGET freerdp3 freerdp-client3 winpr3)
    find_path(LIBYUV_INCLUDE_DIR libyuv/convert.h REQUIRED)
    find_library(LIBYUV_LIBRARY yuv REQUIRED)
    find_package(OpenSSL REQUIRED)

    set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(WITH_GNUTLS OFF CACHE BOOL "" FORCE)
    set(WITH_OPENSSL ON CACHE BOOL "" FORCE)
    set(WITH_GCRYPT OFF CACHE BOOL "" FORCE)
    set(WITH_SASL OFF CACHE BOOL "" FORCE)
    set(WITH_SDL OFF CACHE BOOL "" FORCE)
    set(WITH_GTK OFF CACHE BOOL "" FORCE)
    set(WITH_QT OFF CACHE BOOL "" FORCE)
    set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
    set(WITH_SYSTEMD OFF CACHE BOOL "" FORCE)
    set(WITH_XCB OFF CACHE BOOL "" FORCE)
    set(WITH_WEBSOCKETS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libvncserver
        GIT_REPOSITORY https://github.com/LibVNC/libvncserver.git
        GIT_TAG LibVNCServer-0.9.15
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(libvncserver)

    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)
    set(NO_MEDIA OFF CACHE BOOL "" FORCE)
    set(USE_NICE OFF CACHE BOOL "" FORCE)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libdatachannel/CMakeLists.txt")
        add_subdirectory(third_party/libdatachannel EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(libdatachannel
            GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
            GIT_TAG v0.24.5
            GIT_SHALLOW TRUE
            GIT_SUBMODULES_RECURSE TRUE
        )
        FetchContent_MakeAvailable(libdatachannel)
    endif()

    add_library(remote_gateway_streaming
        src/h264_encoder.cpp
        src/http_server.cpp
        src/rdp_frame_source.cpp
        src/session_manager.cpp
        src/target_config.cpp
        src/vnc_bridge.cpp
        src/vnc_ticket_store.cpp
        src/webrtc_server.cpp
        src/webrtc_video_sink.cpp
    )
    target_include_directories(remote_gateway_streaming PUBLIC include PRIVATE
        ${LIBYUV_INCLUDE_DIR} ${libvncserver_SOURCE_DIR}/include ${libvncserver_BINARY_DIR}/include)
    target_compile_features(remote_gateway_streaming PUBLIC cxx_std_20)
    target_link_libraries(remote_gateway_streaming
        PUBLIC remote_gateway_core
        PRIVATE LibDataChannel::LibDataChannel PkgConfig::OPENH264 PkgConfig::OPUS OpenSSL::SSL OpenSSL::Crypto
                PkgConfig::FREERDP Threads::Threads ${LIBYUV_LIBRARY} vncclient vncserver
    )
    target_compile_definitions(remote_gateway_streaming PRIVATE
        REMOTE_GATEWAY_WEB_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/web"
    )
endif()

add_executable(remote-gateway src/main.cpp)
target_link_libraries(remote-gateway PRIVATE remote_gateway_core)
if(REMOTE_GATEWAY_ENABLE_STREAMING)
    target_link_libraries(remote-gateway PRIVATE remote_gateway_streaming OpenSSL::Crypto)
    target_compile_definitions(remote-gateway PRIVATE REMOTE_GATEWAY_ENABLE_STREAMING=1)
endif()

if(REMOTE_GATEWAY_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME remote_gateway_web_ui_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/web_ui_tests.py)
    add_executable(remote_gateway_tests tests/core_tests.cpp)
    target_link_libraries(remote_gateway_tests PRIVATE remote_gateway_core)
    add_test(NAME remote_gateway_tests COMMAND remote_gateway_tests)
    if(REMOTE_GATEWAY_ENABLE_STREAMING)
        add_executable(remote_gateway_target_config_tests tests/target_config_tests.cpp)
        target_compile_features(remote_gateway_target_config_tests PRIVATE cxx_std_20)
        target_link_libraries(remote_gateway_target_config_tests PRIVATE remote_gateway_streaming)
        add_test(NAME remote_gateway_target_config_tests COMMAND remote_gateway_target_config_tests)
        add_executable(remote_gateway_vnc_ticket_tests tests/vnc_ticket_store_tests.cpp)
        target_compile_features(remote_gateway_vnc_ticket_tests PRIVATE cxx_std_20)
        target_link_libraries(remote_gateway_vnc_ticket_tests PRIVATE remote_gateway_streaming)
        add_test(NAME remote_gateway_vnc_ticket_tests COMMAND remote_gateway_vnc_ticket_tests)
        add_executable(remote_gateway_webrtc_smoke tests/webrtc_smoke.cpp)
        target_compile_features(remote_gateway_webrtc_smoke PRIVATE cxx_std_20)
        target_link_libraries(remote_gateway_webrtc_smoke PRIVATE LibDataChannel::LibDataChannel)
    endif()
endif()
