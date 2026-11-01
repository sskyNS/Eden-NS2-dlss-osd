cmake_minimum_required(VERSION 3.21)

project(EdenLauncher VERSION 2.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg)

set(APP_SOURCES
    src/main.cpp
    src/Theme.cpp
    src/IconFactory.cpp
    src/AppConfig.cpp
    src/MainWindow.cpp
    src/widgets/Ui.cpp
    src/core/ProcessUtil.cpp
    src/core/WinUtil.cpp
    src/core/EdenClient.cpp
    src/core/LosslessScaling.cpp
    src/core/AudioMixer.cpp
    src/core/SaveManager.cpp
    src/core/UpdateChecker.cpp
    src/core/BuildService.cpp
    src/pages/DashboardPage.cpp
    src/pages/GameLibraryPage.cpp
    src/pages/ToolsPage.cpp
    src/pages/UpdatesPage.cpp
    src/pages/SettingsPage.cpp
)

set(APP_HEADERS
    src/Theme.h
    src/IconFactory.h
    src/AppConfig.h
    src/MainWindow.h
    src/widgets/Ui.h
    src/core/ProcessUtil.h
    src/core/WinUtil.h
    src/core/EdenClient.h
    src/core/LosslessScaling.h
    src/core/AudioMixer.h
    src/core/SaveManager.h
    src/core/UpdateChecker.h
    src/core/BuildService.h
    src/pages/DashboardPage.h
    src/pages/GameLibraryPage.h
    src/pages/ToolsPage.h
    src/pages/UpdatesPage.h
    src/pages/SettingsPage.h
)

qt_add_executable(EdenLauncher WIN32
    ${APP_SOURCES}
    ${APP_HEADERS}
    res/app.rc
)

target_include_directories(EdenLauncher PRIVATE src)

target_link_libraries(EdenLauncher PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    user32
    ole32
    oleaut32
    uuid
    psapi
    dwmapi
    shell32
    version
    winmm
    comctl32
    shlwapi
)

target_compile_definitions(EdenLauncher PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

if(MINGW)
    # 静态链接 MinGW 运行时，避免额外分发 libwinpthread / libgcc / libstdc++
    target_link_options(EdenLauncher PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# 输出到 build/bin，便于 windeployqt 打包
set_target_properties(EdenLauncher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
