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
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>

#include <iostream>

// Nombre del canal por el que un Prince ya abierto recibe prince://watch.
// Depende de la ruta del .exe: dos copias en carpetas distintas (por ejemplo
// probando dos cuentas en la misma PC) no se pisan entre si.
static QString instanceChannelName()
{
    const QByteArray path = QCoreApplication::applicationFilePath().toLower().toUtf8();
    return QStringLiteral("PrinceLauncher-") +
           QString::fromLatin1(QCryptographicHash::hash(path, QCryptographicHash::Sha1).toHex().left(16));
}

// Deja registrada la direccion prince:// para el usuario actual (no pide
// administrador) apuntando a este ejecutable, asi el boton "Ver en vivo" de
// la pagina abre Prince.
static void registerUrlProtocol()
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString command = QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1"));

    QSettings cmdKey("HKEY_CURRENT_USER\\Software\\Classes\\prince\\shell\\open\\command", QSettings::NativeFormat);
    if (cmdKey.value(".").toString() == command)
        return;

    QSettings root("HKEY_CURRENT_USER\\Software\\Classes\\prince", QSettings::NativeFormat);
    root.setValue(".", QStringLiteral("URL:Prince"));
    root.setValue("URL Protocol", QStringLiteral(""));
    cmdKey.setValue(".", command);
    cmdKey.sync();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Smash Remix Launcher");
    QCoreApplication::setOrganizationName("RMG-K");

    QString watchUrl;
    for (const QString& arg : QCoreApplication::arguments())
    {
        if (arg.startsWith(QStringLiteral("prince://"), Qt::CaseInsensitive))
            watchUrl = arg;
    }

    // Si ya hay un Prince abierto (esta misma copia), se le pasa la direccion
    // y este proceso se cierra en vez de abrir una segunda ventana.
    if (!watchUrl.isEmpty())
    {
        QLocalSocket socket;
        socket.connectToServer(instanceChannelName());
        if (socket.waitForConnected(400))
        {
            socket.write(watchUrl.toUtf8());
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
            return 0;
        }
    }

    if (!CoreInit())
    {
        std::cerr << "CoreInit() Failed: " << CoreGetError() << std::endl;
        return 1;
    }

    registerUrlProtocol();

    int exitCode;
    {
        LauncherWindow window;

        QLocalServer server;
        QLocalServer::removeServer(instanceChannelName());
        if (server.listen(instanceChannelName()))
        {
            QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window]() {
                while (QLocalSocket* client = server.nextPendingConnection())
                {
                    QObject::connect(client, &QLocalSocket::readyRead, &window, [client, &window]() {
                        window.handleWatchUrl(QString::fromUtf8(client->readAll()));
                    });
                    QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                }
            });
        }

        window.show();
        if (!watchUrl.isEmpty())
            window.handleWatchUrl(watchUrl);
        exitCode = app.exec();
    }

    CoreShutdown();
    return exitCode;
}
