cmake_minimum_required(VERSION 3.16)

project(bt-speaker-cli
    VERSION 0.1.0
    DESCRIPTION "Headless Bluetooth A2DP speaker daemon bridging to PulseAudio/PipeWire/ALSA"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(BT_SPEAKER_CLI_IS_TOP_LEVEL ON)
else()
    set(BT_SPEAKER_CLI_IS_TOP_LEVEL OFF)
endif()

option(BT_SPEAKER_CLI_WITH_PULSE "Build PulseAudio output and BT audio bridge if libpulse is available" ON)
option(BT_SPEAKER_CLI_WITH_DBUS "Build BlueZ D-Bus control if dbus-1 is available" ON)
option(BT_SPEAKER_CLI_WITH_ALSA "Build ALSA output backend if libasound is available" ON)
option(BT_SPEAKER_CLI_WITH_PIPEWIRE "Build PipeWire output backend if libpipewire is available" ON)

set(BT_SPEAKER_CLI_BACKENDS null stdout)
set(BT_SPEAKER_CLI_PULSE_ENABLED OFF)
set(BT_SPEAKER_CLI_DBUS_ENABLED OFF)
set(BT_SPEAKER_CLI_ALSA_ENABLED OFF)
set(BT_SPEAKER_CLI_PIPEWIRE_ENABLED OFF)
set(BT_SPEAKER_CLI_PIPEWIRE_MIN_VERSION 0.3.64)

find_package(PkgConfig QUIET)
find_package(Threads REQUIRED)

if(BT_SPEAKER_CLI_WITH_PULSE)
    if(PkgConfig_FOUND)
        pkg_check_modules(PULSE QUIET IMPORTED_TARGET libpulse)
    endif()
    if(TARGET PkgConfig::PULSE)
        set(BT_SPEAKER_CLI_PULSE_ENABLED ON)
        list(APPEND BT_SPEAKER_CLI_BACKENDS pulse)
    else()
        message(STATUS "bt-speaker-cli: libpulse not found -- building without PulseAudio backend")
    endif()
endif()

if(BT_SPEAKER_CLI_WITH_DBUS)
    if(PkgConfig_FOUND)
        pkg_check_modules(DBUS QUIET IMPORTED_TARGET dbus-1)
    endif()
    if(TARGET PkgConfig::DBUS)
        set(BT_SPEAKER_CLI_DBUS_ENABLED ON)
    else()
        message(STATUS "bt-speaker-cli: dbus-1 not found -- building without BlueZ D-Bus control")
    endif()
endif()

if(BT_SPEAKER_CLI_WITH_PIPEWIRE)
    if(PkgConfig_FOUND)
        pkg_check_modules(PIPEWIRE QUIET IMPORTED_TARGET
            libpipewire-0.3>=${BT_SPEAKER_CLI_PIPEWIRE_MIN_VERSION})
    endif()
    if(TARGET PkgConfig::PIPEWIRE)
        set(BT_SPEAKER_CLI_PIPEWIRE_ENABLED ON)
        list(APPEND BT_SPEAKER_CLI_BACKENDS pipewire)
    else()
        message(STATUS "bt-speaker-cli: libpipewire-0.3 >= ${BT_SPEAKER_CLI_PIPEWIRE_MIN_VERSION} not found -- building without PipeWire backend")
    endif()
endif()

if(BT_SPEAKER_CLI_WITH_ALSA)
    find_package(ALSA)
    if(ALSA_FOUND)
        set(BT_SPEAKER_CLI_ALSA_ENABLED ON)
        list(APPEND BT_SPEAKER_CLI_BACKENDS alsa)
    else()
        message(STATUS "bt-speaker-cli: libasound not found -- building without ALSA backend")
    endif()
endif()

option(BT_SPEAKER_CLI_WERROR "Turn warnings from bt-speaker-cli sources into errors" OFF)
set(BT_SPEAKER_CLI_WARNING_FLAGS -Wall -Wextra -Wpedantic)
if(BT_SPEAKER_CLI_WERROR)
    list(APPEND BT_SPEAKER_CLI_WARNING_FLAGS -Werror)
endif()

add_library(bt-speaker-cli-core STATIC
    src/audio_sink.cpp
    src/bt_audio_bridge.cpp
    src/bt_manager.cpp
    src/bt_reconnect.cpp
    src/cli.cpp
    src/config_file.cpp
    src/control_client.cpp
    src/control_common.cpp
    src/control_socket.cpp
    src/daemon.cpp
    src/hooks.cpp
    src/hooks_posix.cpp
    src/key_value_file.cpp
    src/log.cpp
    src/null_sink.cpp
    src/pcm_ring.cpp
    src/pcm_volume.cpp
    src/sink_recovery.cpp
    src/state_store.cpp
)

target_include_directories(bt-speaker-cli-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bt-speaker-cli-core PUBLIC Threads::Threads)
target_compile_options(bt-speaker-cli-core PRIVATE ${BT_SPEAKER_CLI_WARNING_FLAGS})
target_compile_definitions(bt-speaker-cli-core PUBLIC
    BT_SPEAKER_CLI_VERSION="${PROJECT_VERSION}"
)

if(BT_SPEAKER_CLI_PULSE_ENABLED)
    target_sources(bt-speaker-cli-core PRIVATE src/pulse_sink.cpp)
    target_link_libraries(bt-speaker-cli-core PUBLIC PkgConfig::PULSE)
    target_compile_definitions(bt-speaker-cli-core PUBLIC BT_SPEAKER_CLI_HAVE_PULSE)
endif()

if(BT_SPEAKER_CLI_DBUS_ENABLED)
    target_link_libraries(bt-speaker-cli-core PUBLIC PkgConfig::DBUS)
    target_compile_definitions(bt-speaker-cli-core PUBLIC BT_SPEAKER_CLI_HAVE_DBUS)
endif()

if(BT_SPEAKER_CLI_PIPEWIRE_ENABLED)
    target_sources(bt-speaker-cli-core PRIVATE src/pipewire_sink.cpp)
    target_link_libraries(bt-speaker-cli-core PUBLIC PkgConfig::PIPEWIRE)
    target_compile_definitions(bt-speaker-cli-core PUBLIC BT_SPEAKER_CLI_HAVE_PIPEWIRE)
endif()

if(BT_SPEAKER_CLI_ALSA_ENABLED)
    target_sources(bt-speaker-cli-core PRIVATE src/alsa_sink.cpp)
    target_link_libraries(bt-speaker-cli-core PUBLIC ALSA::ALSA)
    target_compile_definitions(bt-speaker-cli-core PUBLIC BT_SPEAKER_CLI_HAVE_ALSA)
endif()

add_executable(bt-speaker-cli src/main.cpp)
target_link_libraries(bt-speaker-cli PRIVATE bt-speaker-cli-core)
target_compile_options(bt-speaker-cli PRIVATE ${BT_SPEAKER_CLI_WARNING_FLAGS})

list(JOIN BT_SPEAKER_CLI_BACKENDS ", " BT_SPEAKER_CLI_BACKEND_LIST)
message(STATUS "bt-speaker-cli ${PROJECT_VERSION}")
message(STATUS "bt-speaker-cli audio backends: ${BT_SPEAKER_CLI_BACKEND_LIST}")
message(STATUS "bt-speaker-cli BlueZ D-Bus: ${BT_SPEAKER_CLI_DBUS_ENABLED}")

if(BT_SPEAKER_CLI_IS_TOP_LEVEL)
    include(GNUInstallDirs)

    set(BT_SPEAKER_CLI_COMPONENT bt-speaker-cli)
    set(BT_SPEAKER_CLI_DOC_DIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/bt-speaker-cli")

    install(TARGETS bt-speaker-cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT ${BT_SPEAKER_CLI_COMPONENT}
    )

    install(FILES
        README.md
        LICENSE
        NOTICE
        packaging/bt-speaker-cli.conf.example
        DESTINATION ${BT_SPEAKER_CLI_DOC_DIR}
        COMPONENT ${BT_SPEAKER_CLI_COMPONENT}
    )

    set(BT_SPEAKER_CLI_INSTALLED_BINARY "${CMAKE_INSTALL_FULL_BINDIR}/bt-speaker-cli")
    configure_file(packaging/bt-speaker-cli.service.in
        ${CMAKE_CURRENT_BINARY_DIR}/bt-speaker-cli.service @ONLY)
    configure_file(packaging/bt-speaker-cli.user.service.in
        ${CMAKE_CURRENT_BINARY_DIR}/bt-speaker-cli.user.service @ONLY)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bt-speaker-cli.service
            DESTINATION lib/systemd/system
            COMPONENT ${BT_SPEAKER_CLI_COMPONENT}
        )
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bt-speaker-cli.user.service
            DESTINATION lib/systemd/user
            RENAME bt-speaker-cli.service
            COMPONENT ${BT_SPEAKER_CLI_COMPONENT}
        )
        install(FILES packaging/bt-speaker-cli.sysusers.conf
            DESTINATION lib/sysusers.d
            RENAME bt-speaker-cli.conf
            COMPONENT ${BT_SPEAKER_CLI_COMPONENT}
        )
        install(FILES packaging/bt-speaker-cli.dbus.conf
            DESTINATION share/dbus-1/system.d
            RENAME bt-speaker-cli.conf
            COMPONENT ${BT_SPEAKER_CLI_COMPONENT}
        )
    endif()
endif()

option(BT_SPEAKER_CLI_BUILD_TESTS "Build the unit tests" ${BT_SPEAKER_CLI_IS_TOP_LEVEL})
set(GOOGLETEST_GIT_TAG "v1.17.0" CACHE STRING "GoogleTest git tag to build tests against")

if(BT_SPEAKER_CLI_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        ${GOOGLETEST_GIT_TAG}
        GIT_SHALLOW    TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "Install GoogleTest alongside our own targets")
    set(BUILD_GMOCK OFF CACHE BOOL "Build GoogleMock")
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)

    add_executable(bt-speaker-cli-tests
        tests/bt_reconnect_test.cpp
        tests/cli_test.cpp
        tests/config_file_test.cpp
        tests/control_test.cpp
        tests/daemon_test.cpp
        tests/device_spec_test.cpp
        tests/hooks_test.cpp
        tests/null_sink_test.cpp
        tests/pcm_ring_test.cpp
        tests/pcm_volume_test.cpp
        tests/sink_recovery_test.cpp
        tests/state_store_test.cpp
    )
    target_link_libraries(bt-speaker-cli-tests PRIVATE bt-speaker-cli-core GTest::gtest_main)
    target_compile_options(bt-speaker-cli-tests PRIVATE ${BT_SPEAKER_CLI_WARNING_FLAGS})
    gtest_discover_tests(bt-speaker-cli-tests)
endif()
