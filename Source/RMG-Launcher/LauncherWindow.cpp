#include "LauncherWindow.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSettings>
#include <QProcess>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QByteArray>

// Misma anon key publica que usa la web (config.js) y RMG-K (OnlineBridge.cpp)
// -- nunca la service_role key.
static const char* const SUPABASE_URL = "https://jexxpiumbulpuashtjia.supabase.co";
static const char* const SUPABASE_ANON_KEY = "sb_publishable_AS3AbHJZ-nMJsdRkZbXFAA_M6aeTWVB";

// Mismo servidor de Lobby que usa RMG-K.
static const char* const LOBBY_SERVER_URL = "ws://216.128.157.98:8080/ws";

// Convencion de nombres de sala para desafios: el que reta crea una sala
// llamada "RETO:<nickname del rival>"; el rival la detecta escaneando la
// lista de salas buscando una dirigida a su propio nickname. Misma
// convencion que ya usaba OnlineBridge.cpp dentro del juego.
static const char* const CHALLENGE_ROOM_PREFIX = "RETO:";

// Este launcher es especifico de Smash Remix -- no hay selector de ROM.
static const char* const SMASH_REMIX_ROM_NAME = "SMASH REMIX";
static const char* const SMASH_REMIX_ROM_MD5 = "8d72d42b5fa390b0d5e84b5f62b24c9c";

// Replica los mismos umbrales que src/js/config.js en la pagina web.
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

LauncherWindow::LauncherWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Smash Remix Launcher"));
    resize(420, 560);

    buildUi();
    applyStylesheet();

    m_network = new QNetworkAccessManager(this);
    connect(m_network, &QNetworkAccessManager::finished, this, &LauncherWindow::onResolveCodeReply);

    m_rankNetwork = new QNetworkAccessManager(this);
    connect(m_rankNetwork, &QNetworkAccessManager::finished, this, &LauncherWindow::onRankLookupReply);

    m_lobbyClient = new LobbyClient(this);
    connect(m_lobbyClient, &LobbyClient::stateChanged, this, &LauncherWindow::onLobbyStateChanged);
    connect(m_lobbyClient, &LobbyClient::helloFailed, this, &LauncherWindow::onLobbyHelloFailed);
    connect(m_lobbyClient, &LobbyClient::connectError, this, &LauncherWindow::onLobbyConnectError);
    connect(m_lobbyClient, &LobbyClient::presenceFull, this, &LauncherWindow::onLobbyPresenceChanged);
    connect(m_lobbyClient, &LobbyClient::userAdded, this, [this](quint64) { onLobbyPresenceChanged(); });
    connect(m_lobbyClient, &LobbyClient::userRemoved, this, [this](quint64) { onLobbyPresenceChanged(); });
    connect(m_lobbyClient, &LobbyClient::userUpdated, this, [this](quint64) { onLobbyPresenceChanged(); });
    connect(m_lobbyClient, &LobbyClient::roomListChanged, this, &LauncherWindow::onLobbyRoomListChanged);
    connect(m_lobbyClient, &LobbyClient::roomCreated, this, &LauncherWindow::onLobbyRoomCreated);
    connect(m_lobbyClient, &LobbyClient::roomCreateFailed, this, &LauncherWindow::onLobbyRoomCreateFailed);
    connect(m_lobbyClient, &LobbyClient::roomJoinOk, this, &LauncherWindow::onLobbyRoomJoinOk);
    connect(m_lobbyClient, &LobbyClient::roomJoinFailed, this, &LauncherWindow::onLobbyRoomJoinFailed);

    const QString savedCode = QSettings("RMG-K", "n02").value("Launcher/PlayerCode").toString();
    if (!savedCode.isEmpty())
    {
        m_codeInput->setText(savedCode);
        resolveAndConnect(savedCode);
    }
}

LauncherWindow::~LauncherWindow() = default;

void LauncherWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("SMASH REMIX"), this);
    title->setObjectName("brandTitle");
    root->addWidget(title);

    // ---- Login ----
    auto* codeRow = new QHBoxLayout();
    m_codeInput = new QLineEdit(this);
    m_codeInput->setPlaceholderText(QStringLiteral("Tu codigo de jugador"));
    m_connectBtn = new QPushButton(QStringLiteral("Conectar"), this);
    codeRow->addWidget(m_codeInput, 1);
    codeRow->addWidget(m_connectBtn);
    root->addLayout(codeRow);

    connect(m_connectBtn, &QPushButton::clicked, this, &LauncherWindow::onConnectClicked);
    connect(m_codeInput, &QLineEdit::returnPressed, this, &LauncherWindow::onConnectClicked);

    m_myNicknameLabel = new QLabel(this);
    m_myNicknameLabel->setObjectName("nicknameLabel");
    root->addWidget(m_myNicknameLabel);

    m_statusLabel = new QLabel(QStringLiteral("Escribi tu codigo y confirma con Conectar."), this);
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    // ---- Incoming challenge banner ----
    m_incomingBanner = new QWidget(this);
    m_incomingBanner->setObjectName("incomingBanner");
    auto* bannerLayout = new QVBoxLayout(m_incomingBanner);
    m_incomingLabel = new QLabel(m_incomingBanner);
    m_incomingLabel->setObjectName("incomingLabel");
    m_incomingLabel->setWordWrap(true);
    bannerLayout->addWidget(m_incomingLabel);
    auto* bannerBtnRow = new QHBoxLayout();
    m_acceptBtn = new QPushButton(QStringLiteral("Aceptar"), m_incomingBanner);
    m_acceptBtn->setObjectName("acceptBtn");
    m_declineBtn = new QPushButton(QStringLiteral("Rechazar"), m_incomingBanner);
    bannerBtnRow->addWidget(m_acceptBtn);
    bannerBtnRow->addWidget(m_declineBtn);
    bannerLayout->addLayout(bannerBtnRow);
    m_incomingBanner->setVisible(false);
    root->addWidget(m_incomingBanner);

    connect(m_acceptBtn, &QPushButton::clicked, this, &LauncherWindow::onAcceptChallengeClicked);
    connect(m_declineBtn, &QPushButton::clicked, this, &LauncherWindow::onDeclineChallengeClicked);

    // ---- Presence ----
    auto* onlineLabel = new QLabel(QStringLiteral("-- ONLINE --"), this);
    onlineLabel->setObjectName("sectionLabel");
    root->addWidget(onlineLabel);

    m_presenceList = new QListWidget(this);
    root->addWidget(m_presenceList, 1);
    connect(m_presenceList, &QListWidget::itemDoubleClicked, this, &LauncherWindow::onPresenceItemDoubleClicked);

    // ---- Challenge ----
    auto* challengeRow = new QHBoxLayout();
    m_challengeTargetInput = new QLineEdit(this);
    m_challengeTargetInput->setPlaceholderText(QStringLiteral("Nickname a desafiar"));
    m_challengeBtn = new QPushButton(QStringLiteral("Desafiar"), this);
    m_challengeBtn->setEnabled(false);
    challengeRow->addWidget(m_challengeTargetInput, 1);
    challengeRow->addWidget(m_challengeBtn);
    root->addLayout(challengeRow);

    connect(m_challengeBtn, &QPushButton::clicked, this, &LauncherWindow::onChallengeButtonClicked);
    connect(m_challengeTargetInput, &QLineEdit::returnPressed, this, &LauncherWindow::onChallengeButtonClicked);
}

void LauncherWindow::applyStylesheet()
{
    setStyleSheet(R"(
        QWidget { background: #010a13; color: #cdbe9e; font-family: "Segoe UI", sans-serif; font-size: 13px; }
        #brandTitle { color: #f0e6d2; font-size: 22px; font-weight: 700; letter-spacing: 2px; }
        #sectionLabel { color: #0ac8b9; font-weight: 700; letter-spacing: 1px; margin-top: 6px; }
        #nicknameLabel { color: #f0e6d2; font-weight: 600; }
        #statusLabel { color: #a09b8c; }
        QLineEdit {
            background: #010a13; border: 1px solid #785a28; color: #f0e6d2;
            padding: 8px; border-radius: 2px;
        }
        QLineEdit:focus { border-color: #0ac8b9; }
        QPushButton {
            background: #0b1c2c; border: 1px solid #c8aa6e; color: #f0e6d2;
            padding: 8px 16px; border-radius: 2px; font-weight: 600;
        }
        QPushButton:hover:!disabled { background: #c8aa6e; color: #0a0f16; }
        QPushButton:disabled { color: #5a5548; border-color: #3a3020; }
        QListWidget {
            background: #0b1c2c; border: 1px solid #785a28; border-radius: 2px;
            padding: 4px;
        }
        QListWidget::item { padding: 6px; }
        QListWidget::item:hover { background: rgba(10, 200, 185, 0.12); }
        #incomingBanner {
            background: #241a06; border: 1px solid #f5c542; border-radius: 3px; padding: 10px;
        }
        #incomingLabel { color: #f5c542; font-weight: 700; }
        #acceptBtn { border-color: #0ac8b9; }
        #acceptBtn:hover { background: #0ac8b9; color: #010a13; }
    )");
}

void LauncherWindow::setStatus(const QString& text)
{
    m_statusLabel->setText(text);
}

void LauncherWindow::onConnectClicked()
{
    const QString code = m_codeInput->text().trimmed();
    if (code.isEmpty())
        return;
    resolveAndConnect(code);
}

void LauncherWindow::resolveAndConnect(const QString& code)
{
    m_myCode = code;
    m_codeInput->setEnabled(false);
    m_connectBtn->setEnabled(false);
    setStatus(QStringLiteral("Resolviendo codigo..."));

    QNetworkRequest req(QUrl(QString("%1/rest/v1/rpc/resolve_player_code").arg(SUPABASE_URL)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);

    QJsonObject body;
    body["p_code"] = code;
    m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void LauncherWindow::onResolveCodeReply(QNetworkReply* reply)
{
    reply->deleteLater();

    const bool netError = reply->error() != QNetworkReply::NoError;
    QString nickname;
    if (!netError)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isArray() && !doc.array().isEmpty())
        {
            nickname = doc.array().first().toObject().value("nickname").toString();
        }
    }

    if (nickname.isEmpty())
    {
        setStatus(netError ? QStringLiteral("Error de conexion. Intenta de nuevo.")
                            : QStringLiteral("Codigo no encontrado."));
        m_codeInput->setEnabled(true);
        m_connectBtn->setEnabled(true);
        return;
    }

    m_myNickname = nickname;
    m_myNicknameLabel->setText(QStringLiteral("Conectado como: %1").arg(nickname));
    m_codeInput->setEnabled(true);
    m_connectBtn->setEnabled(true);

    QSettings("RMG-K", "n02").setValue("Launcher/PlayerCode", m_myCode);

    setStatus(QStringLiteral("Conectando al Lobby..."));
    m_lobbyClient->connectToServer(LOBBY_SERVER_URL, nickname, {});
}

void LauncherWindow::onLobbyStateChanged(LobbyClient::ConnectionState state)
{
    using State = LobbyClient::ConnectionState;
    switch (state)
    {
    case State::Connected:
        setStatus(QStringLiteral("Conectado como %1").arg(m_myNickname));
        m_challengeBtn->setEnabled(true);
        break;
    case State::Connecting:
    case State::Authenticating:
        setStatus(QStringLiteral("Conectando al Lobby..."));
        m_challengeBtn->setEnabled(false);
        break;
    case State::Disconnected:
        m_challengeBtn->setEnabled(false);
        m_presenceList->clear();
        break;
    case State::Failed:
        setStatus(QStringLiteral("No se pudo conectar al Lobby."));
        m_challengeBtn->setEnabled(false);
        break;
    }
}

void LauncherWindow::onLobbyHelloFailed(const QString& reason)
{
    setStatus(QStringLiteral("Error del servidor: %1").arg(reason));
}

void LauncherWindow::onLobbyConnectError(const QString& message)
{
    setStatus(QStringLiteral("No se pudo conectar: %1").arg(message));
}

void LauncherWindow::onLobbyPresenceChanged()
{
    refreshPresenceList();
}

void LauncherWindow::refreshPresenceList()
{
    if (m_lobbyClient == nullptr)
        return;

    m_presenceList->clear();
    QStringList nicknames;
    for (auto it = m_lobbyClient->users().constBegin(); it != m_lobbyClient->users().constEnd(); ++it)
    {
        if (it->username == m_myNickname)
            continue; // no listarme a mi mismo
        nicknames << it->username;
    }

    for (const QString& nick : nicknames)
    {
        auto* item = new QListWidgetItem(m_presenceList);
        item->setData(Qt::UserRole, nick);
        item->setText(m_rankCache.contains(nick)
            ? QStringLiteral("%1  %2").arg(tierForPoints(m_rankCache.value(nick)), nick)
            : nick);
    }

    fetchRanksForPresence(nicknames);
}

void LauncherWindow::fetchRanksForPresence(const QStringList& nicknames)
{
    if (nicknames.isEmpty())
        return;

    QStringList encoded;
    for (const QString& nick : nicknames)
        encoded << QString::fromUtf8(QUrl::toPercentEncoding(nick));

    const QString filter = QStringLiteral("nickname=in.(%1)").arg(encoded.join(QStringLiteral(",")));
    QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/profiles?select=nickname,rank_points&%2")
        .arg(SUPABASE_URL, filter)));
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);

    m_rankNetwork->get(req);
}

void LauncherWindow::onRankLookupReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() == QNetworkReply::NoError)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isArray())
        {
            for (const QJsonValue& v : doc.array())
            {
                const QJsonObject row = v.toObject();
                m_rankCache[row.value("nickname").toString()] = row.value("rank_points").toDouble();
            }
        }
    }

    for (int i = 0; i < m_presenceList->count(); ++i)
    {
        QListWidgetItem* item = m_presenceList->item(i);
        const QString nick = item->data(Qt::UserRole).toString();
        item->setText(m_rankCache.contains(nick)
            ? QStringLiteral("%1  %2").arg(tierForPoints(m_rankCache.value(nick)), nick)
            : nick);
    }
}

void LauncherWindow::onPresenceItemDoubleClicked()
{
    QListWidgetItem* item = m_presenceList->currentItem();
    if (item == nullptr)
        return;
    m_challengeTargetInput->setText(item->data(Qt::UserRole).toString());
}

void LauncherWindow::onChallengeButtonClicked()
{
    const QString target = m_challengeTargetInput->text().trimmed();
    if (target.isEmpty() || m_lobbyClient == nullptr)
        return;
    if (m_lobbyClient->state() != LobbyClient::ConnectionState::Connected)
        return;
    sendChallenge(target);
}

void LauncherWindow::sendChallenge(const QString& targetNickname)
{
    m_challengeBtn->setEnabled(false);
    m_pendingChallengeTarget = targetNickname;
    setStatus(QStringLiteral("Enviando desafio a %1...").arg(targetNickname));

    m_lobbyClient->createRoom(
        QString(CHALLENGE_ROOM_PREFIX) + targetNickname,
        SMASH_REMIX_ROM_NAME,
        SMASH_REMIX_ROM_MD5,
        QString(), // region: resuelta por el servidor segun el MD5
        2,         // maxPlayers: un desafio siempre es 1v1
        -1,        // delay: Auto
        0,         // prediction: Default
        1);        // pacing: Smooth
}

void LauncherWindow::onLobbyRoomCreated(quint64 roomId)
{
    // No nos desconectamos todavia: si lo hicieramos ahora, la sala podria
    // desaparecer antes de que el rival la vea en su lista (bug real que ya
    // encontramos probando esto mismo dentro del juego). Nos quedamos
    // conectados sosteniendola abierta hasta que el rival entre.
    m_hostedChallengeRoomId = roomId;
    m_hostedChallengeOpponent = m_pendingChallengeTarget;
    setStatus(QStringLiteral("Desafio enviado a %1, esperando...").arg(m_hostedChallengeOpponent));
}

void LauncherWindow::onLobbyRoomCreateFailed(const QString& reason)
{
    m_challengeBtn->setEnabled(true);
    setStatus(QStringLiteral("No se pudo enviar el desafio: %1").arg(reason));
}

void LauncherWindow::onLobbyRoomListChanged()
{
    checkIncomingChallenge();

    if (m_hostedChallengeRoomId != 0)
    {
        const auto it = m_lobbyClient->rooms().constFind(m_hostedChallengeRoomId);
        if (it != m_lobbyClient->rooms().constEnd() && it->players >= 2)
        {
            const quint64 roomId = m_hostedChallengeRoomId;
            const QString opponent = m_hostedChallengeOpponent;
            m_hostedChallengeRoomId = 0;
            m_hostedChallengeOpponent.clear();
            setStatus(QStringLiteral("%1 acepto! Abriendo la partida...").arg(opponent));
            handOffToGame(roomId, m_myNickname);
        }
    }
}

void LauncherWindow::checkIncomingChallenge()
{
    if (m_lobbyClient == nullptr || m_myNickname.isEmpty())
        return;

    const QString myRoomName = QString(CHALLENGE_ROOM_PREFIX) + m_myNickname;
    quint64 foundRoomId = 0;
    QString foundHost;

    for (auto it = m_lobbyClient->rooms().constBegin(); it != m_lobbyClient->rooms().constEnd(); ++it)
    {
        if (it->name == myRoomName && it->state == QStringLiteral("waiting"))
        {
            foundRoomId = it->id;
            foundHost = it->hostName;
            break;
        }
    }

    if (foundRoomId != 0 && foundRoomId != m_incomingChallengeRoomId)
    {
        m_incomingChallengeRoomId = foundRoomId;
        m_incomingChallengerName = foundHost;
        showIncomingChallenge(foundHost);
    }
    else if (foundRoomId == 0 && m_incomingChallengeRoomId != 0)
    {
        // La sala ya no esta (el que reto se fue, o cerro el launcher).
        m_incomingChallengeRoomId = 0;
        m_incomingChallengerName.clear();
        clearIncomingChallenge();
    }
}

void LauncherWindow::showIncomingChallenge(const QString& challenger)
{
    m_incomingLabel->setText(QStringLiteral("%1 te desafio!").arg(challenger));
    m_incomingBanner->setVisible(true);
    m_acceptBtn->setEnabled(true);
    m_declineBtn->setEnabled(true);
}

void LauncherWindow::clearIncomingChallenge()
{
    m_incomingBanner->setVisible(false);
}

void LauncherWindow::onAcceptChallengeClicked()
{
    if (m_incomingChallengeRoomId == 0)
        return;
    m_acceptBtn->setEnabled(false);
    m_declineBtn->setEnabled(false);
    setStatus(QStringLiteral("Uniendose a la partida..."));
    m_lobbyClient->joinRoom(m_incomingChallengeRoomId);
}

void LauncherWindow::onDeclineChallengeClicked()
{
    m_incomingChallengeRoomId = 0;
    m_incomingChallengerName.clear();
    clearIncomingChallenge();
}

void LauncherWindow::onLobbyRoomJoinOk(quint64 roomId)
{
    if (roomId != m_incomingChallengeRoomId && roomId != m_hostedChallengeRoomId)
        return;

    const QString opponent = m_incomingChallengerName;
    m_incomingChallengeRoomId = 0;
    m_incomingChallengerName.clear();
    clearIncomingChallenge();
    setStatus(QStringLiteral("Conectado! Abriendo la partida..."));
    handOffToGame(roomId, m_myNickname);
    Q_UNUSED(opponent);
}

void LauncherWindow::onLobbyRoomJoinFailed(const QString& reason)
{
    m_acceptBtn->setEnabled(true);
    m_declineBtn->setEnabled(true);
    setStatus(QStringLiteral("No se pudo unir a la partida: %1").arg(reason));
}

void LauncherWindow::handOffToGame(quint64 roomId, const QString& nickname)
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString rmgkPath = exeDir + QStringLiteral("/RMG-K.exe");

    QStringList args;
    args << QStringLiteral("--join-lobby-room=%1").arg(roomId)
         << QStringLiteral("--lobby-nickname=%1").arg(nickname);

    QProcess::startDetached(rmgkPath, args, exeDir);

    if (m_lobbyClient != nullptr)
    {
        m_lobbyClient->disconnectFromServer();
    }
    setStatus(QStringLiteral("Partida en curso. Podes dejar esta ventana abierta o cerrarla."));
}
