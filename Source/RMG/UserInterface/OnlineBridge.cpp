#include "OnlineBridge.hpp"

#ifdef NETPLAY

#include <RMG-Core/RomSettings.hpp>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QByteArray>
#include <QUrl>
#include <QHash>

namespace UserInterface
{

// Safe to embed: this is the Supabase "publishable" (anon) key, the same one
// used by the public website. It only grants what Row Level Security allows
// an anonymous visitor — never the secret/service_role key.
static const char* const SUPABASE_URL = "https://jexxpiumbulpuashtjia.supabase.co";
static const char* const SUPABASE_ANON_KEY = "sb_publishable_AS3AbHJZ-nMJsdRkZbXFAA_M6aeTWVB";
static const char* const SHARED_MEM_NAME = "Local\\SmashRemixOnlineBridge";

// Same production lobby endpoint RollbackLobbyDialog/LobbyConnectDialog use
// (see LobbyConnectDialog.cpp: kDefaultLobbyUrl).
static const char* const LOBBY_SERVER_URL = "ws://216.128.157.98:8080/ws";

// Room-naming convention for challenges: no lobby server changes needed.
// The challenger creates a room named "RETO:<target nickname>"; the target's
// own bridge watches the room list for one addressed to them.
static const char* const CHALLENGE_ROOM_PREFIX = "RETO:";

// Replica los mismos umbrales que src/js/config.js en la pagina web, para que
// el rango mostrado en el juego siempre coincida con el sitio.
static QString tierForPoints(double points)
{
    if (points >= 300) return QStringLiteral("S");
    if (points >= 200) return QStringLiteral("A+");
    if (points >= 160) return QStringLiteral("A");
    if (points >= 100) return QStringLiteral("B+");
    if (points >= 80)  return QStringLiteral("B");
    if (points >= 50)  return QStringLiteral("C+");
    if (points >= 30)  return QStringLiteral("C");
    return QStringLiteral("D");
}

OnlineBridge::OnlineBridge(QObject* parent) : QObject(parent)
{
    if (!openSharedMemory())
    {
        return;
    }

    m_network = new QNetworkAccessManager(this);
    connect(m_network, &QNetworkAccessManager::finished, this, &OnlineBridge::onResolveCodeReply);

    m_rankNetwork = new QNetworkAccessManager(this);
    connect(m_rankNetwork, &QNetworkAccessManager::finished, this, &OnlineBridge::onRankLookupReply);

    m_lobbyClient = new Dialog::LobbyClient(this);
    connect(m_lobbyClient, &Dialog::LobbyClient::stateChanged, this, &OnlineBridge::onLobbyStateChanged);
    connect(m_lobbyClient, &Dialog::LobbyClient::presenceFull, this, &OnlineBridge::onLobbyPresenceChanged);
    connect(m_lobbyClient, &Dialog::LobbyClient::userAdded, this, [this](quint64) { onLobbyPresenceChanged(); });
    connect(m_lobbyClient, &Dialog::LobbyClient::userRemoved, this, [this](quint64) { onLobbyPresenceChanged(); });
    connect(m_lobbyClient, &Dialog::LobbyClient::userUpdated, this, [this](quint64) { onLobbyPresenceChanged(); });
    connect(m_lobbyClient, &Dialog::LobbyClient::roomListChanged, this, &OnlineBridge::onLobbyRoomListChanged);
    connect(m_lobbyClient, &Dialog::LobbyClient::roomCreated, this, &OnlineBridge::onLobbyRoomCreated);
    connect(m_lobbyClient, &Dialog::LobbyClient::roomCreateFailed, this, &OnlineBridge::onLobbyRoomCreateFailed);
    connect(m_lobbyClient, &Dialog::LobbyClient::roomJoinOk, this, &OnlineBridge::onLobbyRoomJoinOk);
    connect(m_lobbyClient, &Dialog::LobbyClient::roomJoinFailed, this, &OnlineBridge::onLobbyRoomJoinFailed);

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &OnlineBridge::pollSharedMemory);
    m_pollTimer->start(250);
}

OnlineBridge::~OnlineBridge()
{
    if (m_lobbyClient != nullptr)
    {
        m_lobbyClient->disconnectFromServer();
    }
    if (m_shared != nullptr)
    {
        UnmapViewOfFile(m_shared);
    }
    if (m_mapping != nullptr)
    {
        CloseHandle(m_mapping);
    }
}

bool OnlineBridge::openSharedMemory()
{
    const DWORD size = sizeof(OnlineBridgeShared);

    m_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, SHARED_MEM_NAME);
    if (m_mapping == nullptr)
    {
        return false;
    }

    const bool alreadyExisted = (GetLastError() == ERROR_ALREADY_EXISTS);

    m_shared = static_cast<OnlineBridgeShared*>(MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, size));
    if (m_shared == nullptr)
    {
        CloseHandle(m_mapping);
        m_mapping = nullptr;
        return false;
    }

    if (!alreadyExisted)
    {
        memset(m_shared, 0, size);
        memcpy(m_shared->signature, "SRONLINEV1", 11);
    }

    return true;
}

void OnlineBridge::connectToLobbyIfNeeded()
{
    if (m_lobbyClient == nullptr || m_myNickname.isEmpty())
    {
        return;
    }
    if (m_lobbyClient->state() == Dialog::LobbyClient::ConnectionState::Connected ||
        m_lobbyClient->state() == Dialog::LobbyClient::ConnectionState::Connecting)
    {
        return;
    }
    m_lobbyClient->connectToServer(LOBBY_SERVER_URL, m_myNickname, {});
}

void OnlineBridge::onLobbyStateChanged(Dialog::LobbyClient::ConnectionState state)
{
    if (state == Dialog::LobbyClient::ConnectionState::Connected)
    {
        refreshPresence();
    }
}

void OnlineBridge::onLobbyPresenceChanged()
{
    refreshPresence();
}

void OnlineBridge::refreshPresence()
{
    if (m_shared == nullptr || m_lobbyClient == nullptr)
    {
        return;
    }

    const auto& users = m_lobbyClient->users();
    uint32_t count = 0;
    QStringList rawNicknames;
    memset(m_shared->onlineNicknames, 0, sizeof(m_shared->onlineNicknames));

    for (auto it = users.constBegin(); it != users.constEnd() && count < ONLINE_BRIDGE_MAX_PRESENCE; ++it)
    {
        if (it->username == m_myNickname)
        {
            continue; // no listarme a mi mismo
        }
        // Mostrar el nickname solo mientras se resuelve el rango; se
        // reemplaza por "{rango} {nickname}" en cuanto responda Supabase.
        const QByteArray utf8 = it->username.toUtf8().left(23);
        memcpy(m_shared->onlineNicknames[count], utf8.constData(), utf8.size());
        rawNicknames << it->username;
        count++;
    }
    m_shared->onlineCount = count;

    m_pendingPresenceNicknames = rawNicknames;
    fetchRanksForPresence(rawNicknames);
}

void OnlineBridge::fetchRanksForPresence(const QStringList& nicknames)
{
    if (m_rankNetwork == nullptr || nicknames.isEmpty())
    {
        return;
    }

    QStringList encoded;
    for (const QString& nick : nicknames)
    {
        encoded << QString::fromUtf8(QUrl::toPercentEncoding(nick));
    }

    const QString filter = QStringLiteral("nickname=in.(%1)").arg(encoded.join(QStringLiteral(",")));
    QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/profiles?select=nickname,rank_points&%2")
        .arg(SUPABASE_URL, filter)));
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);

    m_rankNetwork->get(req);
}

void OnlineBridge::onRankLookupReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (m_shared == nullptr)
    {
        return;
    }

    QHash<QString, double> pointsByNickname;
    if (reply->error() == QNetworkReply::NoError)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isArray())
        {
            for (const QJsonValue& v : doc.array())
            {
                const QJsonObject row = v.toObject();
                pointsByNickname[row["nickname"].toString()] = row["rank_points"].toDouble();
            }
        }
    }
    // Si fallo la consulta, pointsByNickname queda vacio y cada nombre se
    // muestra tal cual (sin rango) en vez de trabarse esperando.

    uint32_t count = 0;
    memset(m_shared->onlineNicknames, 0, sizeof(m_shared->onlineNicknames));
    for (const QString& nick : m_pendingPresenceNicknames)
    {
        if (count >= ONLINE_BRIDGE_MAX_PRESENCE)
        {
            break;
        }
        QString line = nick;
        const auto found = pointsByNickname.constFind(nick);
        if (found != pointsByNickname.constEnd())
        {
            line = QStringLiteral("%1 %2").arg(tierForPoints(found.value()), nick);
        }
        const QByteArray utf8 = line.toUtf8().left(23);
        memcpy(m_shared->onlineNicknames[count], utf8.constData(), utf8.size());
        count++;
    }
    m_shared->onlineCount = count;
}

void OnlineBridge::onLobbyRoomListChanged()
{
    checkIncomingChallenge();
}

void OnlineBridge::checkIncomingChallenge()
{
    if (m_lobbyClient == nullptr || m_shared == nullptr || m_myNickname.isEmpty())
    {
        return;
    }

    const QString myRoomName = QString(CHALLENGE_ROOM_PREFIX) + m_myNickname;
    quint64 foundRoomId = 0;
    QString foundHost;

    const auto& rooms = m_lobbyClient->rooms();
    for (auto it = rooms.constBegin(); it != rooms.constEnd(); ++it)
    {
        if (it->name == myRoomName && it->state == QStringLiteral("waiting"))
        {
            foundRoomId = it->id;
            foundHost = it->hostName;
            break;
        }
    }

    m_incomingChallengeRoomId = foundRoomId;
    m_incomingChallengerName = foundHost;

    memset(m_shared->incomingChallenger, 0, sizeof(m_shared->incomingChallenger));
    if (!foundHost.isEmpty())
    {
        const QByteArray utf8 = foundHost.toUtf8().left(23);
        memcpy(m_shared->incomingChallenger, utf8.constData(), utf8.size());
    }
}

void OnlineBridge::sendChallenge(uint32_t requestId, const QString& targetNickname)
{
    if (m_lobbyClient == nullptr || targetNickname.isEmpty())
    {
        writeStatus(requestId, ONLINE_BRIDGE_STATUS_ERROR);
        return;
    }

    CoreRomSettings romSettings;
    if (!CoreGetCurrentRomSettings(romSettings))
    {
        writeStatus(requestId, ONLINE_BRIDGE_STATUS_ERROR);
        return;
    }

    m_pendingRoomRequestId = requestId;
    const QString roomName = QString(CHALLENGE_ROOM_PREFIX) + targetNickname;
    m_lobbyClient->createRoom(
        roomName,
        QString::fromStdString(romSettings.GoodName),
        QString::fromStdString(romSettings.MD5),
        QString(), // region: resolved by the server from the MD5, same as the manual Create Room dialog
        2,         // maxPlayers: a challenge is always 1v1
        -1,        // delay: Auto
        0,         // prediction: Default
        1);        // pacing: Smooth (the only mode the engine actually uses)
}

void OnlineBridge::acceptChallenge(uint32_t requestId)
{
    if (m_lobbyClient == nullptr || m_incomingChallengeRoomId == 0)
    {
        writeStatus(requestId, ONLINE_BRIDGE_STATUS_ERROR);
        return;
    }
    m_pendingRoomRequestId = requestId;
    m_lobbyClient->joinRoom(m_incomingChallengeRoomId);
}

void OnlineBridge::onLobbyRoomCreated(quint64 roomId)
{
    if (m_pendingRoomRequestId != 0)
    {
        writeStatus(m_pendingRoomRequestId, ONLINE_BRIDGE_STATUS_FOUND);
        m_pendingRoomRequestId = 0;
    }
    handOffRoomToLobbyDialog(roomId, /*isHost=*/true);
}

void OnlineBridge::onLobbyRoomCreateFailed(const QString& reason)
{
    Q_UNUSED(reason);
    if (m_pendingRoomRequestId != 0)
    {
        writeStatus(m_pendingRoomRequestId, ONLINE_BRIDGE_STATUS_NOT_FOUND);
        m_pendingRoomRequestId = 0;
    }
}

void OnlineBridge::onLobbyRoomJoinOk(quint64 roomId)
{
    if (m_pendingRoomRequestId != 0)
    {
        writeStatus(m_pendingRoomRequestId, ONLINE_BRIDGE_STATUS_FOUND);
        m_pendingRoomRequestId = 0;
    }
    // Ya en la sala; limpiar el aviso de desafio pendiente.
    m_incomingChallengeRoomId = 0;
    m_incomingChallengerName.clear();
    if (m_shared != nullptr)
    {
        memset(m_shared->incomingChallenger, 0, sizeof(m_shared->incomingChallenger));
    }
    handOffRoomToLobbyDialog(roomId, /*isHost=*/false);
}

// La sala ya existe en el servidor (creada o unida por esta misma conexion
// oculta). A partir de aca, el arranque real de la partida -- ICE, ping,
// asignacion de seats, sincronizacion pre-match -- lo maneja el
// RollbackLobbyDialog real (ver RollbackLobbyDialog::autoJoinRoomOnConnect),
// no este puente headless. Por eso soltamos esta conexion (el servidor no
// admite dos sesiones con el mismo nickname a la vez) y le avisamos a
// MainWindow para que abra ese dialogo y se una a la misma sala por id.
void OnlineBridge::handOffRoomToLobbyDialog(quint64 roomId, bool isHost)
{
    const QString nickname = m_myNickname;
    if (m_lobbyClient != nullptr)
    {
        m_lobbyClient->disconnectFromServer();
    }
    emit challengeRoomReady(roomId, nickname, isHost);
}

void OnlineBridge::onLobbyRoomJoinFailed(const QString& reason)
{
    Q_UNUSED(reason);
    if (m_pendingRoomRequestId != 0)
    {
        writeStatus(m_pendingRoomRequestId, ONLINE_BRIDGE_STATUS_ERROR);
        m_pendingRoomRequestId = 0;
    }
}

void OnlineBridge::pollSharedMemory()
{
    if (m_shared == nullptr)
    {
        return;
    }

    if (memcmp(m_shared->signature, "SRONLINEV1", 10) != 0)
    {
        return;
    }

    const uint32_t requestId = m_shared->requestId;
    if (requestId == 0 || requestId == m_lastSeenRequestId)
    {
        return; // nothing new
    }
    m_lastSeenRequestId = requestId;

    const uint32_t command = m_shared->command;

    if (command == ONLINE_BRIDGE_CMD_RESOLVE_CODE)
    {
        if (m_requestInFlight) return;

        char rawCode[9] = {0};
        memcpy(rawCode, m_shared->inputCode, 8);
        const QString code = QString::fromLatin1(rawCode).trimmed();

        m_shared->status = ONLINE_BRIDGE_STATUS_WORKING; // intermediate: responseId stays behind on purpose

        if (code.isEmpty())
        {
            writeStatus(requestId, ONLINE_BRIDGE_STATUS_ERROR);
            return;
        }

        QNetworkRequest req(QUrl(QString("%1/rest/v1/rpc/resolve_player_code").arg(SUPABASE_URL)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("apikey", SUPABASE_ANON_KEY);
        req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);
        req.setAttribute(QNetworkRequest::User, requestId);

        QJsonObject body;
        body["p_code"] = code;

        m_requestInFlight = true;
        m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    else if (command == ONLINE_BRIDGE_CMD_SEND_CHALLENGE)
    {
        char rawTarget[25] = {0};
        memcpy(rawTarget, m_shared->challengeTarget, 24);
        const QString target = QString::fromUtf8(rawTarget).trimmed();
        sendChallenge(requestId, target);
    }
    else if (command == ONLINE_BRIDGE_CMD_ACCEPT_CHALLENGE)
    {
        acceptChallenge(requestId);
    }
}

void OnlineBridge::onResolveCodeReply(QNetworkReply* reply)
{
    reply->deleteLater();
    m_requestInFlight = false;

    const uint32_t requestId = reply->request().attribute(QNetworkRequest::User).toUInt();

    if (reply->error() != QNetworkReply::NoError)
    {
        writeStatus(requestId, ONLINE_BRIDGE_STATUS_ERROR);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray() || doc.array().isEmpty())
    {
        writeStatus(requestId, ONLINE_BRIDGE_STATUS_NOT_FOUND);
        return;
    }

    const QJsonObject row = doc.array().first().toObject();
    const QString nickname = row["nickname"].toString();
    writeStatus(requestId, ONLINE_BRIDGE_STATUS_FOUND, nickname);

    // Ahora que sabemos quienes somos publicamente, conectarse al lobby con
    // esa misma identidad para que la presencia (quien esta online) funcione.
    if (!nickname.isEmpty())
    {
        m_myNickname = nickname;
        connectToLobbyIfNeeded();
    }
}

void OnlineBridge::writeStatus(uint32_t requestId, uint32_t status, const QString& nickname)
{
    if (m_shared == nullptr)
    {
        return;
    }

    m_shared->status = status;
    if (!nickname.isEmpty())
    {
        const QByteArray utf8 = nickname.toUtf8().left(23);
        memset(m_shared->resultNickname, 0, sizeof(m_shared->resultNickname));
        memcpy(m_shared->resultNickname, utf8.constData(), utf8.size());
    }
    m_shared->responseId = requestId;
}

} // namespace UserInterface

#endif // NETPLAY
