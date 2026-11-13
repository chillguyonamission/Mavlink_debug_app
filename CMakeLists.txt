cmake_minimum_required(VERSION 3.16)
project(MavlinkDebugApp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# --- Gremsy PayloadSDK (git submodule, pinned) + this app's patches, applied in order ---
set(SDK_DIR ${CMAKE_SOURCE_DIR}/third_party/PayloadSdk)
set(SDK_PATCHES ${CMAKE_SOURCE_DIR}/patches/payloadsdk-transport-observer.patch
                ${CMAKE_SOURCE_DIR}/patches/payloadsdk-windows.patch)
if(NOT EXISTS ${SDK_DIR}/libs/CMakeLists.txt)
    message(FATAL_ERROR "PayloadSDK missing: run  git submodule update --init")
endif()
list(GET SDK_PATCHES -1 sdk_last_patch)
execute_process(COMMAND git apply --reverse --check ${sdk_last_patch}
                WORKING_DIRECTORY ${SDK_DIR} RESULT_VARIABLE sdk_unpatched OUTPUT_QUIET ERROR_QUIET)
if(NOT sdk_unpatched EQUAL 0)  # the last patch is not in yet: apply each one that is missing
    foreach(p ${SDK_PATCHES})
        execute_process(COMMAND git apply --reverse --check ${p}
                        WORKING_DIRECTORY ${SDK_DIR} RESULT_VARIABLE missing OUTPUT_QUIET ERROR_QUIET)
        if(NOT missing EQUAL 0)
            execute_process(COMMAND git apply ${p} WORKING_DIRECTORY ${SDK_DIR} RESULT_VARIABLE rc)
            if(NOT rc EQUAL 0)
                message(FATAL_ERROR "Could not apply ${p} to ${SDK_DIR}")
            endif()
            message(STATUS "Applied ${p}")
        endif()
    endforeach()
endif()

if(MSVC)
    set(APP_WARNINGS /W3)
else()
    set(APP_WARNINGS -Wall -Wextra)
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTVIDEO REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
if(WIN32)
    # Npcap SDK (headers, import library). The user installs the Npcap runtime (its licence forbids
    # bundling), so wpcap.dll is delay-loaded and PcapCapture checks that it loads before any pcap call.
    set(NPCAP_SDK "$ENV{NPCAP_SDK}" CACHE PATH "Npcap SDK folder (Include/, Lib/x64/)")
    if(NOT EXISTS ${NPCAP_SDK}/Include/pcap/pcap.h)
        message(FATAL_ERROR "Npcap SDK not found: set NPCAP_SDK (https://npcap.com/#download)")
    endif()
    add_library(PkgConfig::PCAP INTERFACE IMPORTED)
    target_include_directories(PkgConfig::PCAP INTERFACE ${NPCAP_SDK}/Include)
    target_link_libraries(PkgConfig::PCAP INTERFACE ${NPCAP_SDK}/Lib/x64/wpcap.lib delayimp)
    target_link_options(PkgConfig::PCAP INTERFACE /DELAYLOAD:wpcap.dll)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
else()
    pkg_check_modules(PCAP REQUIRED IMPORTED_TARGET libpcap)
endif()
add_subdirectory(${SDK_DIR}/libs ${CMAKE_BINARY_DIR}/payloadsdk)  # target: PayloadSDK
target_link_libraries(PayloadSDK Threads::Threads)  # plain signature: the SDK's own CMake uses it

# --- Qt-free MAVLink core (framing, decoding) ---
add_library(mavcore STATIC src/mavlink_frame.cpp)
target_include_directories(mavcore PUBLIC src ${SDK_DIR}/libs/third-party/mavlink/include/mavlink/v2.0)
target_compile_options(mavcore PRIVATE ${APP_WARNINGS})

# --- App logic without widgets: SDK ownership, link health, frame model ---
add_library(appcore STATIC
    src/link_health.cpp src/payload_link.cpp src/frame_model.cpp src/frame_filter.cpp
    src/frame_text.cpp src/frame_writer.cpp src/flow_stats.cpp src/mavlink_dict.cpp
    src/command_tracker.cpp src/telemetry.cpp src/detection.cpp src/misb0601.cpp src/metadata_model.cpp src/payload_profile_vio.cpp src/payload_profile_zio.cpp src/payload_profile_mb1.cpp src/payload_profile_orusl.cpp src/profiles.cpp src/pcap_capture.cpp)
target_include_directories(appcore PUBLIC src ${SDK_DIR}/libs)  # libs: detection_packet.h, which has no other SDK dependency
target_link_libraries(appcore PUBLIC Qt6::Gui mavcore PkgConfig::PCAP PRIVATE PayloadSDK)
target_compile_options(appcore PRIVATE ${APP_WARNINGS})
# XML definitions matching the generated headers; see MavlinkDict::defaultDir()
target_compile_definitions(appcore PRIVATE
    MAVLINK_XML_DIR="${SDK_DIR}/libs/third-party/mavlink/include/mavlink/v2.0/message_definitions")

# --- Widgets and the GUI application ---
add_library(appgui STATIC src/theme.cpp src/main_window.cpp src/inspector_widget.cpp src/stats_widget.cpp src/payload_pages.cpp
    src/video_player.cpp src/video_page.cpp src/klv_receiver.cpp src/klv_page.cpp src/splash.cpp)
target_link_libraries(appgui PUBLIC Qt6::Widgets appcore PRIVATE PkgConfig::GSTVIDEO)
if(WIN32)
    target_link_libraries(appgui PRIVATE iphlpapi)  # if_nametoindex
endif()
target_compile_options(appgui PRIVATE ${APP_WARNINGS})

add_executable(mavlink_debug_app src/main.cpp)
target_link_libraries(mavlink_debug_app PRIVATE appgui)

# --- Tests (no hardware needed) ---
enable_testing()
add_executable(test_mavlink_frame tests/test_mavlink_frame.cpp)
target_link_libraries(test_mavlink_frame mavcore)
add_test(NAME mavlink_frame COMMAND test_mavlink_frame)

add_executable(test_sdk_transport tests/test_sdk_transport.cpp)
target_link_libraries(test_sdk_transport PayloadSDK mavcore)
add_test(NAME sdk_transport COMMAND test_sdk_transport)
set_tests_properties(sdk_transport PROPERTIES TIMEOUT 30)  # a shutdown hang fails instead of blocking

pkg_check_modules(GSTRTSPSERVER IMPORTED_TARGET gstreamer-rtsp-server-1.0)  # test only: local RTSP server
if(GSTRTSPSERVER_FOUND)
    add_executable(test_video tests/test_video.cpp)
    target_link_libraries(test_video PRIVATE Qt6::Test appgui PkgConfig::GSTRTSPSERVER PkgConfig::GSTVIDEO)
    add_test(NAME video COMMAND test_video)
    set_tests_properties(video PROPERTIES TIMEOUT 60 ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()

add_executable(test_klv_stream tests/test_klv_stream.cpp)
target_link_libraries(test_klv_stream PRIVATE Qt6::Test appgui PkgConfig::GSTVIDEO)
add_test(NAME klv_stream COMMAND test_klv_stream)
set_tests_properties(klv_stream PROPERTIES TIMEOUT 300 ENVIRONMENT QT_QPA_PLATFORM=offscreen)

set(APP_TESTS link_logic payload_link inspector payload_control pcap main_window detection misb0601 metadata_model)
if(WIN32)
    list(REMOVE_ITEM APP_TESTS pcap)  # calls libpcap directly; CI runners cannot install Npcap (no silent free install)
endif()
foreach(t ${APP_TESTS})
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE Qt6::Test appgui)
    add_test(NAME ${t} COMMAND test_${t})
    set_tests_properties(${t} PROPERTIES TIMEOUT 60 ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endforeach()

target_compile_definitions(test_payload_control PRIVATE SDK_PAYLOAD_DEFINE_DIR="${SDK_DIR}/libs/payload-define")

# --- Headless capture tool (hardware checks, field recordings) ---
add_executable(mavlink_capture tools/mavlink_capture.cpp)
target_link_libraries(mavlink_capture PRIVATE appcore)

# Hardware check (moves a real gimbal): built, never run by ctest
add_executable(hw_vio_commands tests/hw_vio_commands.cpp)
target_link_libraries(hw_vio_commands PRIVATE appcore)

# --- Install: the apps plus the MAVLink XML they read units/enum names from (MavlinkDict::defaultDir) ---
include(GNUInstallDirs)
install(TARGETS mavlink_debug_app mavlink_capture RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${SDK_DIR}/libs/third-party/mavlink/include/mavlink/v2.0/message_definitions/
        DESTINATION ${CMAKE_INSTALL_BINDIR}/message_definitions FILES_MATCHING PATTERN "*.xml")
