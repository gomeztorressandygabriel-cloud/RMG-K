#
# Smash Remix Launcher CMakeLists.txt
#
# Standalone "lobby client" app (separate .exe from RMG-K, same idea as the
# Slippi launcher for Melee): lets a player save their web account's ID once,
# see who else has the launcher open, send/accept challenges, and on accept
# launches RMG-K already pointed at the matched lobby room -- the player never
# touches RMG-K's own Lobby UI.
#
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.2 COMPONENTS Gui Widgets Core Network WebSockets Concurrent REQUIRED)

set(RMG_LAUNCHER_SOURCES
    main.cpp
    LauncherWindow.cpp
    # Reused as-is from RMG's own Rollback Lobby -- same server, same
    # protocol, already tested. Compiled a second time into this separate
    # executable rather than split into a shared library, to keep this a
    # small, low-risk addition alongside the existing RMG target.
    ../RMG/UserInterface/Dialog/Lobby/LobbyClient.cpp
)

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_executable(RMG-Launcher WIN32 ${RMG_LAUNCHER_SOURCES})
else()
    add_executable(RMG-Launcher ${RMG_LAUNCHER_SOURCES})
endif()

set_target_properties(RMG-Launcher PROPERTIES OUTPUT_NAME "Smash Remix Launcher")

target_include_directories(RMG-Launcher PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../RMG/UserInterface
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

add_definitions(-DNETPLAY)

target_link_libraries(RMG-Launcher
    RMG-Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::Concurrent
)

if (PORTABLE_INSTALL)
    set_target_properties(RMG-Launcher PROPERTIES
        INSTALL_RPATH "$ORIGIN"
    )
endif(PORTABLE_INSTALL)

if (WIN32)
    # ws2_32: LobbyClient calls WSAIoctl directly on the anchor socket.
    target_link_libraries(RMG-Launcher ws2_32)
endif()
