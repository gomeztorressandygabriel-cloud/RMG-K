/*
 * Smash Remix Launcher
 *
 * Standalone lobby client: presence, challenges, notifications. On a
 * matched challenge it launches RMG-K already pointed at the right lobby
 * room, so the player never sees RMG-K's own Lobby UI.
 */
#include "LauncherWindow.hpp"

#include <RMG-Core/Core.hpp>
#include <RMG-Core/Error.hpp>

#include <QApplication>

#include <iostream>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Smash Remix Launcher");
    QCoreApplication::setOrganizationName("RMG-K");

    if (!CoreInit())
    {
        std::cerr << "CoreInit() Failed: " << CoreGetError() << std::endl;
        return 1;
    }

    int exitCode;
    {
        LauncherWindow window;
        window.show();
        exitCode = app.exec();
    }

    CoreShutdown();
    return exitCode;
}
