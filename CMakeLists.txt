cmake_minimum_required(VERSION 3.21)
project(RealtimeNetworkSpeed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Widgets)

qt_add_executable(RealtimeNetworkSpeed WIN32
    main.cpp
    traffic.h
    app.rc
    app_resources.qrc
)

# rc.exe 從建置目錄執行，需要在那裡找得到 app.ico
configure_file(app.ico app.ico COPYONLY)

target_link_libraries(RealtimeNetworkSpeed PRIVATE Qt6::Widgets iphlpapi)
if(MSVC)
    target_compile_options(RealtimeNetworkSpeed PRIVATE /utf-8)
endif()

option(BUILD_VERIFICATION "Build unit and UI verification" OFF)
if(BUILD_VERIFICATION)
    find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Test)
    add_executable(RealtimeNetworkSpeedVerification tests/verification.cpp app_resources.qrc)
    target_link_libraries(RealtimeNetworkSpeedVerification PRIVATE Qt6::Widgets Qt6::Test iphlpapi)
    if(MSVC)
        target_compile_options(RealtimeNetworkSpeedVerification PRIVATE /utf-8)
    endif()
    add_executable(RealtimeNetworkSpeedTrafficTests tests/traffic_tests.cpp)
    if(MSVC)
        target_compile_options(RealtimeNetworkSpeedTrafficTests PRIVATE /utf-8)
    endif()
    enable_testing()
    add_test(NAME traffic-logic COMMAND RealtimeNetworkSpeedTrafficTests)
    add_test(NAME units-and-ui COMMAND RealtimeNetworkSpeedVerification)
    set_tests_properties(units-and-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
endif()
