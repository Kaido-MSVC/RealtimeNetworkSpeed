cmake_minimum_required(VERSION 3.21)
project(RealtimeNetworkSpeed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
# clang-cl uses MSVC-style flags but must retain Clang's own standard mapping.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.52)
        message(FATAL_ERROR "C++23 requires MSVC 19.52 (toolset 14.52) or newer; detected ${CMAKE_CXX_COMPILER_VERSION}.")
    endif()
    # Older CMake versions map cxx_std_23 to c++latest. MSVC 14.52 supports
    # the final spelling; leave a future native CMake mapping unchanged.
    foreach(mode STANDARD EXTENSION)
        if(CMAKE_CXX23_${mode}_COMPILE_OPTION MATCHES "^[-/]std:c\\+\\+latest$")
            set(CMAKE_CXX23_${mode}_COMPILE_OPTION "-std:c++23")
        endif()
    endforeach()
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.12...<6.13 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(RealtimeNetworkSpeed WIN32
    main.cpp
    traffic.h
    history.h
    app.rc
    app_resources.qrc
)

# rc.exe 從建置目錄執行，需要在那裡找得到 app.ico
configure_file(app.ico app.ico COPYONLY)

target_link_libraries(RealtimeNetworkSpeed PRIVATE Qt6::Widgets Qt6::Network iphlpapi)
target_compile_features(RealtimeNetworkSpeed PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(RealtimeNetworkSpeed PRIVATE /utf-8)
endif()

option(BUILD_VERIFICATION "Build unit and UI verification" OFF)
if(BUILD_VERIFICATION)
    find_package(Qt6 6.12...<6.13 REQUIRED COMPONENTS Test)
    add_executable(RealtimeNetworkSpeedVerification tests/verification.cpp app_resources.qrc)
    target_compile_features(RealtimeNetworkSpeedVerification PRIVATE cxx_std_23)
    target_link_libraries(RealtimeNetworkSpeedVerification PRIVATE Qt6::Widgets Qt6::Network Qt6::Test iphlpapi)
    if(MSVC)
        target_compile_options(RealtimeNetworkSpeedVerification PRIVATE /utf-8)
    endif()
    add_executable(RealtimeNetworkSpeedTrafficTests tests/traffic_tests.cpp)
    target_compile_features(RealtimeNetworkSpeedTrafficTests PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(RealtimeNetworkSpeedTrafficTests PRIVATE /utf-8)
    endif()
    enable_testing()
    add_test(NAME traffic-logic COMMAND RealtimeNetworkSpeedTrafficTests)
    add_test(NAME units-and-ui COMMAND RealtimeNetworkSpeedVerification)
    set_tests_properties(units-and-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
endif()
