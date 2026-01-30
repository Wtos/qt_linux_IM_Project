cmake_minimum_required(VERSION 3.10)
project(IMClient VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network)

set(CLIENT_SOURCES
    main.cpp
    ui/chatwindow.cpp
    ui/chatwindow.ui
    ui/loginwindow.cpp
    ui/loginwindow.ui
    network/tcpclient.cpp
    network/protocol.cpp
)

add_executable(IMClient ${CLIENT_SOURCES})

target_link_libraries(IMClient
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

target_include_directories(IMClient PRIVATE
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/network
)

if(WIN32)
    target_link_libraries(IMClient ws2_32)
endif()
