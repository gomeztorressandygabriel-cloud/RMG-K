#include "LauncherWindow.hpp"
#include "AchievementLogic.hpp"

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
#include <QDesktopServices>
#include <QColor>
#include <QMenu>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <algorithm>
#include <cmath>
#include <QTextCursor>
#include <QStackedWidget>
#include <QDateTime>
#include <QMessageBox>
#include <QCheckBox>
#include <QUrl>
#include <QUrlQuery>
#include <QAction>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QEasingCurve>
#include <QAbstractAnimation>
#include <QSize>
#include <QTextEdit>
#include <QScrollBar>
#include <QScrollArea>
#include <QFrame>
#include <QLayoutItem>
#include <QDialog>
#include <QTimer>
#include <QRandomGenerator>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QApplication>
#include <QPainter>
#include <QIcon>
#include <QPixmap>
#include <QFile>
#include <QCryptographicHash>
#include <QDir>
#include <QCloseEvent>
#include <QEventLoop>
#include <QStandardPaths>
#include <QFileInfo>
#include <QWheelEvent>
#include <QAbstractItemView>
#include <windows.h>
#include <mmsystem.h>

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
// Misma idea para la invitacion de Team (arma el par de 2). El partido de 4
// se arma despues, cuando un equipo reta a otro (ver challengeDuo).
static const char* const TEAM_ROOM_PREFIX = "TEAM:";

// Este launcher es especifico de Smash Remix -- no hay selector de ROM.
static const char* const SMASH_REMIX_ROM_NAME = "SMASH REMIX";
// La ROM modificada que se distribuye junto al launcher. OJO: no es la Smash
// Remix normal (esa tiene otro MD5 y es la que RMG-K encontraria solo en su
// biblioteca); las dos partes tienen que correr exactamente esta.
static const char* const SMASH_REMIX_ROM_FILE = "ssb64asm.z64";

static const char* const WEBSITE_URL = "https://smashremix.netlify.app/";

// El manifiesto vive en el repo (pesa nada) y adentro trae las URLs reales
// de cada archivo, que apuntan a GitHub Releases. Publicar una version nueva
// es un commit del json + un release; no hay que recompilar el launcher ni
// tocar el hosting de la web.
static const char* const UPDATE_MANIFEST_URL =
    "https://raw.githubusercontent.com/gomeztorressandygabriel-cloud/smash-remix-online/main/version.json";

// Nivel de experiencia. XP total para llegar al nivel L: 25*(L-1)*(L+6); subir
// del nivel N al siguiente cuesta 50*N + 150. Misma cuenta que la web.
static qint64 xpTotalForLevel(int level)
{
    return 25LL * (level - 1) * (level + 6);
}

static int levelFromXp(qint64 xp)
{
    int level = qMax(1, static_cast<int>(std::floor((-5.0 + std::sqrt(49.0 + xp / 6.25)) / 2.0)));
    while (xpTotalForLevel(level + 1) <= xp)
        ++level;
    while (level > 1 && xpTotalForLevel(level) > xp)
        --level;
    return level;
}

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

// Punto de color de estado. Se lee de un vistazo, a diferencia de un
// "(online)" mezclado con el resto del texto de la fila.
static QIcon statusDot(const QColor& color)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(2, 2, 8, 8);
    return QIcon(pm);
}

// Insignia de rango: hasta ahora la letra salia en gris como el resto del
// texto, asi que no aportaba nada de un vistazo. Con color, la lista se lee
// por jerarquia sin tener que ir leyendo letra por letra.
static QColor tierColor(const QString& tier)
{
    if (tier == QStringLiteral("S"))  return QColor(0xf5, 0xc5, 0x42); // dorado
    if (tier.startsWith(QLatin1Char('A'))) return QColor(0xe0, 0x6c, 0x5a); // rojo suave
    if (tier.startsWith(QLatin1Char('B'))) return QColor(0xc0, 0x8a, 0xe0); // violeta
    if (tier.startsWith(QLatin1Char('C'))) return QColor(0x6f, 0xa8, 0xdc); // azul
    return QColor(0x7d, 0x8a, 0x95);                                       // D: gris
}

// Dibuja la insignia como icono, para poder pintarla de color: un item de
// QListWidget no admite dos colores de texto distintos en la misma fila.
static QIcon tierBadge(const QString& tier, bool dimmed)
{
    QColor c = tierColor(tier);
    if (dimmed)
        c = c.darker(220);

    QPixmap pm(26, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill = c;
    fill.setAlpha(dimmed ? 40 : 55);
    p.setBrush(fill);
    p.setPen(QPen(c, 1));
    p.drawRoundedRect(QRectF(0.5, 0.5, 25, 17), 4, 4);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(8);
    p.setFont(f);
    p.setPen(c);
    p.drawText(QRectF(0, 0, 26, 18), Qt::AlignCenter, tier);
    p.end();
    return QIcon(pm);
}

// Punto de estado + insignia de rango en un solo icono: una fila de lista
// admite un unico icono, y los dos datos tienen que leerse juntos.
static QIcon rosterIcon(const QColor& dot, const QString& tier, bool offline)
{
    QPixmap pm(46, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(Qt::NoPen);
    p.setBrush(dot);
    p.drawEllipse(1, 5, 8, 8);

    QColor c = offline ? tierColor(tier).darker(220) : tierColor(tier);
    QColor fill = c;
    fill.setAlpha(offline ? 35 : 55);
    p.setBrush(fill);
    p.setPen(QPen(c, 1));
    p.drawRoundedRect(QRectF(15.5, 0.5, 29, 17), 4, 4);

    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(8);
    p.setFont(f);
    p.setPen(c);
    p.drawText(QRectF(15, 0, 30, 18), Qt::AlignCenter, tier);
    p.end();
    return QIcon(pm);
}

// Verde/teal = conectado, dorado = jugando, gris apagado = desconectado.
static QColor dotColorForState(const QString& state)
{
    if (state.isEmpty())                            return QColor(0x33, 0x3b, 0x44);
    if (state == QStringLiteral("playing"))         return QColor(0xf5, 0xc5, 0x42);
    return QColor(0x0a, 0xc8, 0xb9);
}

// Alto justo para las secciones con filas: crecen con el contenido hasta un
// tope, en vez de reservar siempre el mismo hueco aunque esten casi vacias.
static void fitScrollHeight(QScrollArea* scroll, int rowCount)
{
    const int rows = qBound(1, rowCount, 3);
    scroll->setFixedHeight(rows * 52 + 4);
}

// Clave corta para la sala de un desafio: viaja embebida en el nombre de la
// sala (ver CHALLENGE_ROOM_PREFIX) porque no hay otro canal privado entre los
// dos launchers antes de que el rival vea la sala en su lista. No es
// criptografia -- solo evita que alguien mas se una por accidente/curiosidad
// a una sala que el servidor ya marca con clave.
static QString generateRoomPassword()
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString pw;
    for (int i = 0; i < 6; ++i)
        pw.append(QLatin1Char(alphabet[QRandomGenerator::global()->bounded(int(sizeof(alphabet) - 1))]));
    return pw;
}

LauncherWindow::LauncherWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Prince"));
    setWindowIcon(QIcon(QStringLiteral(":/prince.ico")));
    resize(980, 700);

    buildUi();
    applyStylesheet();

    // Fundido de entrada de la ventana: liviano (solo opacidad, una vez, se
    // borra sola al terminar).
    auto* windowFadeEffect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(windowFadeEffect);
    windowFadeEffect->setOpacity(0.0);
    auto* windowFade = new QPropertyAnimation(windowFadeEffect, "opacity", this);
    windowFade->setStartValue(0.0);
    windowFade->setEndValue(1.0);
    windowFade->setDuration(260);
    windowFade->setEasingCurve(QEasingCurve::OutCubic);
    // CRITICO: Qt no soporta QGraphicsEffect anidados. Mientras esta ventana
    // conserve el suyo, cualquier widget hijo con efecto propio -- p.ej. el
    // pulso de una fila de desafio -- se pinta completamente invisible. Ese
    // fue el bug de "el desafio llega pero no se ve nada" que nos costo
    // varias rondas: el efecto quedaba pegado a la ventana para siempre
    // porque la animacion se autodestruye pero el efecto no. Se quita apenas
    // termina el fundido, que es lo unico que lo necesitaba.
    connect(windowFade, &QPropertyAnimation::finished, this, [this]() {
        this->setGraphicsEffect(nullptr);
    });
    windowFade->start(QAbstractAnimation::DeleteWhenStopped);

    m_network = new QNetworkAccessManager(this);
    connect(m_network, &QNetworkAccessManager::finished, this, &LauncherWindow::onResolveCodeReply);

    m_rosterNetwork = new QNetworkAccessManager(this);
    connect(m_rosterNetwork, &QNetworkAccessManager::finished, this, &LauncherWindow::onFullRosterReply);

    m_friendsNetwork = new QNetworkAccessManager(this);
    connect(m_friendsNetwork, &QNetworkAccessManager::finished, this, [this](QNetworkReply* reply) {
        const QString op = reply->property("op").toString();
        if (op == QStringLiteral("send"))
            onSendFriendRequestReply(reply);
        else if (op == QStringLiteral("respond"))
            onRespondFriendRequestReply(reply);
        else if (op == QStringLiteral("ach_profile"))
            onAchProfileReply(reply);
        else if (op == QStringLiteral("ach_matches"))
            onAchMatchesReply(reply);
        else if (op == QStringLiteral("levels"))
            onLevelsReply(reply);
        else if (op == QStringLiteral("h2h"))
            onH2hReply(reply);
        else if (op == QStringLiteral("dm_list"))
            onDmListReply(reply);
        else if (op == QStringLiteral("dm_send"))
            onDmSendReply(reply);
        else if (op == QStringLiteral("dm_unread"))
            onDmUnreadReply(reply);
        else
            onListFriendsReply(reply);
    });

    // Presencia para la web: se refresca sola cada 20s. La web da por
    // desconectado a quien lleve mas de 45s sin reportar, asi que un cierre
    // brusco se resuelve solo sin necesidad de aviso.
    // Latido del punto de "en partida": recorre solo las filas que estan
    // jugando y les cambia el icono. Barato y sin efectos graficos.
    m_dotPulseTimer = new QTimer(this);
    m_dotPulseTimer->setInterval(650);
    connect(m_dotPulseTimer, &QTimer::timeout, this, [this]() {
        if (m_presenceList == nullptr || m_myNickname.isEmpty())
            return;
        m_dotPulseOn = !m_dotPulseOn;
        for (int i = 0; i < m_presenceList->count(); ++i)
        {
            QListWidgetItem* item = m_presenceList->item(i);
            const QString nickname = item->data(Qt::UserRole).toString();
            if (lobbyStateFor(nickname) != QStringLiteral("playing"))
                continue;
            QColor dot(0xf5, 0xc5, 0x42);
            if (!m_dotPulseOn)
                dot = dot.darker(190);
            item->setIcon(rosterIcon(dot, item->data(Qt::UserRole + 1).toString(), false));
        }
    });
    m_dotPulseTimer->start();

    m_updateNetwork = new QNetworkAccessManager(this);
    // Restos de una actualizacion previa: el .exe viejo solo se puede borrar
    // cuando ya no se esta ejecutando, o sea en el arranque siguiente.
    QFile::remove(QCoreApplication::applicationFilePath() + QStringLiteral(".old"));
    for (const char* leftover : {"RMG-K.exe.old", "Plugin/Audio/RMG-Audio.dll.old", "ssb64asm.z64.old"})
        QFile::remove(QDir(QCoreApplication::applicationDirPath()).filePath(QString::fromLatin1(leftover)));

    m_presenceNetwork = new QNetworkAccessManager(this);
    connect(m_presenceNetwork, &QNetworkAccessManager::finished,
            this, [](QNetworkReply* reply) { reply->deleteLater(); });
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(20000);
    connect(m_presenceTimer, &QTimer::timeout, this, &LauncherWindow::reportPresence);

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
    connect(m_lobbyClient, &LobbyClient::chatMessageReceived, this, &LauncherWindow::onLobbyChatMessageReceived);
    connect(m_lobbyClient, &LobbyClient::pingProbeMeasured, this, &LauncherWindow::onLobbyPingProbeMeasured);

    if (QSystemTrayIcon::isSystemTrayAvailable())
    {
        m_trayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/prince.ico")), this);
        m_trayIcon->setToolTip(QStringLiteral("Prince"));
        m_trayIcon->show();
    }

    // NOTA: hubo un intento de "refrescar" la lista de salas reconectando
    // cada pocos segundos, pero el servidor (compartido con jugadores
    // reales) lo trato como abuso y devolvio HTTP 429, bloqueando la IP
    // temporalmente. Se saco por completo -- reconectar tan seguido no es
    // seguro contra un servidor de produccion compartido. La deteccion de
    // desafios entrantes vuelve a depender solo de checkIncomingChallenges()
    // via roomListChanged (organico, sin forzar reconexiones).

    // MD5 de la ROM modificada propia (64 MB, ~0.2 s una sola vez al abrir).
    {
        QFile rom(romFilePath());
        if (rom.open(QIODevice::ReadOnly))
        {
            QCryptographicHash hash(QCryptographicHash::Md5);
            if (hash.addData(&rom))
                m_romMd5 = QString::fromLatin1(hash.result().toHex());
        }
        if (m_romMd5.isEmpty())
            setStatus(QStringLiteral("No encontre %1 junto al launcher.").arg(SMASH_REMIX_ROM_FILE));
    }

    fetchFullRoster();
    fetchLevels();

    // Primero actualizar, despues conectar. El archivo "no-autoupdate.txt" junto
    // al .exe lo desactiva (copias de prueba que no deben pisarse).
    QTimer::singleShot(900, this, [this]() {
        showPendingUpdateNotes();
        showWelcomeIfFirstRun();
    });
    const bool skipUpdate = QFile::exists(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("no-autoupdate.txt")));
    if (skipUpdate)
    {
        startAutoConnect();
    }
    else
    {
        m_startupUpdateCheck = true;
        QTimer::singleShot(0, this, [this]() {
            setUpdateStatus(QStringLiteral("Buscando actualizaciones..."));
            checkForUpdates();
        });
    }
}

void LauncherWindow::startAutoConnect()
{
    const QString savedCode = QSettings("RMG-K", "n02").value("Launcher/PlayerCode").toString();
    if (!savedCode.isEmpty())
    {
        m_codeInput->setText(savedCode);

        // Se autoconecta solo si este es el unico launcher abierto. Con dos
        // ventanas (probando dos cuentas en la misma PC) las dos cargaban el
        // mismo codigo guardado y se conectaban en el mismo instante: misma
        // cuenta dos veces, y encima chocando contra el limite de conexiones
        // por IP del servidor. Si ya hay otro abierto, el codigo igual queda
        // escrito y alcanza con apretar Conectar.
        const HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"SmashRemixLauncherInstance");
        const bool anotherLauncherOpen =
            (instanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS);
        if (!anotherLauncherOpen)
            resolveAndConnect(savedCode);
        else
            setStatus(QStringLiteral("Ya hay otro launcher abierto: apreta Conectar."));
    }
}

LauncherWindow::~LauncherWindow() = default;

void LauncherWindow::buildUi()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ==================== Panel izquierdo: cuenta / acciones ====================
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(300);
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(28, 32, 28, 28);
    side->setSpacing(16);

    // Marca: titulo + bajada juntos y pegados, sin margenes negativos (antes
    // el "LAUNCHER" quedaba montado/recortado contra el titulo).
    auto* brand = new QWidget(sidebar);
    auto* brandBox = new QVBoxLayout(brand);
    brandBox->setContentsMargins(0, 0, 0, 0);
    brandBox->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("SMASH<span style=\"color:#0ac8b9;\">REMIX</span>"), brand);
    title->setObjectName("brandTitle");
    title->setTextFormat(Qt::RichText);
    brandBox->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral("LAUNCHER"), brand);
    subtitle->setObjectName("brandSubtitle");
    brandBox->addWidget(subtitle);
    side->addWidget(brand);

    // Caja de cuenta: se oculta entera al conectar (el codigo ya no hace
    // falta y el boton "Conectar" ahi colgado confundia).
    m_accountBox = new QWidget(sidebar);
    auto* accountBox = new QVBoxLayout(m_accountBox);
    accountBox->setContentsMargins(0, 0, 0, 0);
    accountBox->setSpacing(10);

    m_codeInput = new QLineEdit(m_accountBox);
    m_codeInput->setPlaceholderText(QStringLiteral("Tu codigo de jugador"));
    accountBox->addWidget(m_codeInput);

    m_connectBtn = new QPushButton(QStringLiteral("Conectar"), m_accountBox);
    m_connectBtn->setObjectName("primaryBtn");
    accountBox->addWidget(m_connectBtn);
    side->addWidget(m_accountBox);

    connect(m_connectBtn, &QPushButton::clicked, this, &LauncherWindow::onConnectClicked);
    connect(m_codeInput, &QLineEdit::returnPressed, this, &LauncherWindow::onConnectClicked);

    // Tarjeta de cuenta: avatar con la inicial, nombre y estado juntos en un
    // bloque, en vez de tres etiquetas sueltas flotando.
    m_accountCard = new QWidget(sidebar);
    m_accountCard->setObjectName("accountCard");
    m_accountCard->setAttribute(Qt::WA_StyledBackground, true);
    auto* cardRow = new QHBoxLayout(m_accountCard);
    cardRow->setContentsMargins(12, 10, 12, 10);
    cardRow->setSpacing(12);

    m_avatarLabel = new QLabel(m_accountCard);
    m_avatarLabel->setObjectName("avatarCircle");
    m_avatarLabel->setFixedSize(42, 42);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    m_avatarLabel->setAttribute(Qt::WA_StyledBackground, true);
    cardRow->addWidget(m_avatarLabel);

    auto* cardText = new QVBoxLayout();
    cardText->setContentsMargins(0, 0, 0, 0);
    cardText->setSpacing(2);
    m_myNicknameLabel = new QLabel(m_accountCard);
    m_myNicknameLabel->setObjectName("nicknameLabel");
    cardText->addWidget(m_myNicknameLabel);
    m_statusLabel = new QLabel(QStringLiteral("Escribi tu codigo y confirma con Conectar."), m_accountCard);
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setWordWrap(true);
    cardText->addWidget(m_statusLabel);
    cardRow->addLayout(cardText, 1);

    m_accountCard->hide(); // aparece recien cuando hay cuenta resuelta
    m_accountCard->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_accountCard, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(m_accountCard);
        QAction* available = menu.addAction(QStringLiteral("Disponible"));
        available->setCheckable(true);
        available->setChecked(m_presenceMode != QStringLiteral("dnd"));
        QAction* dnd = menu.addAction(QStringLiteral("No molestar (sin sonidos ni avisos)"));
        dnd->setCheckable(true);
        dnd->setChecked(m_presenceMode == QStringLiteral("dnd"));
        menu.addSeparator();
        QAction* logout = menu.addAction(QStringLiteral("Cerrar sesion"));
        QAction* chosen = menu.exec(m_accountCard->mapToGlobal(pos));
        if (chosen == logout)
            logOut();
        else if (chosen == available && m_presenceMode == QStringLiteral("dnd"))
            setPresenceMode(m_searching ? QStringLiteral("searching") : QStringLiteral("online"));
        else if (chosen == dnd)
            setPresenceMode(QStringLiteral("dnd"));
    });
    side->addWidget(m_accountCard);

    // Mientras no hay cuenta, el estado se muestra suelto debajo del codigo.
    m_preLoginStatus = new QLabel(QStringLiteral("Escribi tu codigo y confirma con Conectar."), sidebar);
    m_preLoginStatus->setObjectName("statusLabel");
    m_preLoginStatus->setWordWrap(true);
    side->addWidget(m_preLoginStatus);

    side->addSpacing(8);
    auto* settingsBtn = new QPushButton(QStringLiteral("  Configuracion del juego"), sidebar);
    settingsBtn->setObjectName("ghostBtn");
    settingsBtn->setCursor(Qt::PointingHandCursor);
    side->addWidget(settingsBtn);
    auto* settingsMenu = new QMenu(settingsBtn);
    const QList<QPair<QString, QString>> settingsEntries = {
        {QStringLiteral("Graficos"), QStringLiteral("video")},
        {QStringLiteral("Sonido"), QStringLiteral("audio")},
        {QStringLiteral("Controles"), QStringLiteral("input")},
    };
    for (const auto& entry : settingsEntries)
    {
        const QString kind = entry.second;
        settingsMenu->addAction(entry.first, this, [this, kind]() {
            const QString exeDir = QCoreApplication::applicationDirPath();
            QProcess::startDetached(exeDir + QStringLiteral("/RMG-K.exe"),
                                    {QStringLiteral("--open-settings=%1").arg(kind)}, exeDir);
        });
    }
    settingsMenu->addSeparator();
    settingsMenu->addAction(QStringLiteral("Diagnostico de conexion"), this, [this]() { showDiagnostics(); });
    connect(settingsBtn, &QPushButton::clicked, this, [settingsBtn, settingsMenu]() {
        settingsMenu->exec(settingsBtn->mapToGlobal(QPoint(0, -settingsMenu->sizeHint().height())));
    });

    m_searchBtn = new QPushButton(QStringLiteral("  Buscar partida"), sidebar);
    m_searchBtn->setObjectName("ghostBtn");
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    side->addWidget(m_searchBtn);
    connect(m_searchBtn, &QPushButton::clicked, this, [this]() {
        if (m_myNickname.isEmpty() || !m_lobbyConnected)
        {
            setStatus(QStringLiteral("Conectate al Lobby para buscar partida."));
            return;
        }
        setSearching(!m_searching);
    });

    m_rematchBtn = new QPushButton(sidebar);
    m_rematchBtn->setObjectName("ghostBtn");
    m_rematchBtn->setCursor(Qt::PointingHandCursor);
    m_rematchBtn->hide();
    side->addWidget(m_rematchBtn);
    connect(m_rematchBtn, &QPushButton::clicked, this, [this]() {
        const QString opponent = m_rematchOpponent;
        const QString type = m_rematchType;
        m_rematchBtn->hide();
        m_rematchOpponent.clear();
        if (!opponent.isEmpty())
            challengePlayer(opponent, type);
    });

    side->addSpacing(14);

    auto* chatLabel = new QLabel(QStringLiteral("CHAT"), sidebar);
    chatLabel->setObjectName("sectionLabel");
    side->addWidget(chatLabel);

    m_chatLog = new QTextEdit(sidebar);
    m_chatLog->setObjectName("chatLog");
    m_chatLog->setReadOnly(true);
    m_chatLog->setMinimumHeight(40);
    side->addWidget(m_chatLog, 1);

    auto* chatRow = new QHBoxLayout();
    m_chatInput = new QLineEdit(sidebar);
    m_chatInput->setPlaceholderText(QStringLiteral("Mensaje..."));
    m_chatInput->setEnabled(false);
    m_chatSendBtn = new QPushButton(QStringLiteral("Enviar"), sidebar);
    m_chatSendBtn->setEnabled(false);
    chatRow->addWidget(m_chatInput, 1);
    chatRow->addWidget(m_chatSendBtn);
    side->addLayout(chatRow);

    connect(m_chatSendBtn, &QPushButton::clicked, this, &LauncherWindow::onChatSendClicked);
    connect(m_chatInput, &QLineEdit::returnPressed, this, &LauncherWindow::onChatSendClicked);

    // Acciones secundarias al pie, chicas y juntas: antes eran tres botones
    // grandes apilados arriba, que se comian la mejor parte de la barra y
    // competian con Conectar.
    side->addSpacing(10);
    auto* footerRule = new QFrame(sidebar);
    footerRule->setObjectName("sectionRule");
    footerRule->setFrameShape(QFrame::HLine);
    side->addWidget(footerRule);

    m_websiteBtn = new QPushButton(QStringLiteral("  Pagina web"), sidebar);
    m_websiteBtn->setObjectName("ghostBtn");
    m_websiteBtn->setCursor(Qt::PointingHandCursor);
    side->addWidget(m_websiteBtn);
    connect(m_websiteBtn, &QPushButton::clicked, this, &LauncherWindow::onWebsiteButtonClicked);

    m_shortcutBtn = new QPushButton(QStringLiteral("  Acceso directo en el escritorio"), sidebar);
    m_shortcutBtn->setObjectName("ghostBtn");
    m_shortcutBtn->setCursor(Qt::PointingHandCursor);
    side->addWidget(m_shortcutBtn);
    connect(m_shortcutBtn, &QPushButton::clicked, this, &LauncherWindow::onCreateShortcutClicked);

    m_updateBtn = new QPushButton(QStringLiteral("  Buscar actualizacion"), sidebar);
    m_updateBtn->setObjectName("ghostBtn");
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    side->addWidget(m_updateBtn);
    connect(m_updateBtn, &QPushButton::clicked, this, &LauncherWindow::onCheckUpdateClicked);

    root->addWidget(sidebar);

    // ==================== Panel derecho: presencia ====================
    auto* main = new QWidget(this);
    main->setObjectName("mainPanel");
    auto* mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(28, 32, 28, 28);
    mainLayout->setSpacing(14);

    // Desafios pendientes (enviados y recibidos) -- primero, porque son lo
    // mas urgente. La seccion entera se oculta cuando no hay ninguno.
    {
        m_pendingSection = new QWidget(main);
        auto* box = new QVBoxLayout(m_pendingSection);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(8);

        auto* pendingLabel = new QLabel(QStringLiteral("DESAFIOS PENDIENTES"), m_pendingSection);
        pendingLabel->setObjectName("sectionLabelHot");
        box->addWidget(pendingLabel);

        m_pendingScroll = new QScrollArea(m_pendingSection);
        m_pendingScroll->setObjectName("pendingScroll");
        m_pendingScroll->setWidgetResizable(true);
        m_pendingScroll->setFrameShape(QFrame::NoFrame);
        m_pendingScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* container = new QWidget(m_pendingScroll);
        m_pendingLayout = new QVBoxLayout(container);
        m_pendingLayout->setContentsMargins(0, 0, 4, 0);
        m_pendingLayout->setSpacing(6);
        m_pendingLayout->addStretch(1);
        m_pendingScroll->setWidget(container);
        box->addWidget(m_pendingScroll);

        m_pendingSection->hide();
        mainLayout->addWidget(m_pendingSection);
    }

    // Solicitudes de amistad recibidas -- buzon aparte de la lista general
    // de amigos, para que no se pierdan entre los ya agregados.
    {
        m_friendRequestsSection = new QWidget(main);
        auto* box = new QVBoxLayout(m_friendRequestsSection);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(8);

        auto* requestsLabel = new QLabel(QStringLiteral("SOLICITUDES DE AMISTAD"), m_friendRequestsSection);
        requestsLabel->setObjectName("sectionLabelHot");
        box->addWidget(requestsLabel);

        m_friendRequestsScroll = new QScrollArea(m_friendRequestsSection);
        m_friendRequestsScroll->setObjectName("friendRequestsScroll");
        m_friendRequestsScroll->setWidgetResizable(true);
        m_friendRequestsScroll->setFrameShape(QFrame::NoFrame);
        m_friendRequestsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* container = new QWidget(m_friendRequestsScroll);
        m_friendRequestsLayout = new QVBoxLayout(container);
        m_friendRequestsLayout->setContentsMargins(0, 0, 4, 0);
        m_friendRequestsLayout->setSpacing(6);
        m_friendRequestsLayout->addStretch(1);
        m_friendRequestsScroll->setWidget(container);
        box->addWidget(m_friendRequestsScroll);

        m_friendRequestsSection->hide();
        mainLayout->addWidget(m_friendRequestsSection);
    }

    // Pestañas: la misma lista grande sirve para Jugadores y para Amigos,
    // en vez de meter a los amigos en una cajita de 130 px con scroll.
    auto* onlineHeader = new QHBoxLayout();
    onlineHeader->setSpacing(6);
    m_tabPlayersBtn = new QPushButton(QStringLiteral("JUGADORES"), main);
    m_tabFriendsBtn = new QPushButton(QStringLiteral("AMIGOS"), main);
    for (QPushButton* tab : {m_tabPlayersBtn, m_tabFriendsBtn})
    {
        tab->setObjectName("tabBtn");
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        onlineHeader->addWidget(tab);
    }
    m_tabPlayersBtn->setChecked(true);
    // Linea fina que ocupa el resto del ancho: ata el titulo con la lista y
    // marca la separacion sin necesidad de otro recuadro.
    auto* onlineRule = new QFrame(main);
    onlineRule->setObjectName("sectionRule");
    onlineRule->setFrameShape(QFrame::HLine);
    onlineHeader->addWidget(onlineRule, 1);
    m_onlineCountLabel = new QLabel(main);
    m_onlineCountLabel->setObjectName("hintLabel");
    onlineHeader->addWidget(m_onlineCountLabel);
    mainLayout->addLayout(onlineHeader);

    m_listStack = new QStackedWidget(main);

    m_presenceList = new QListWidget(main);
    m_presenceList->setSpacing(4);
    // Sin esto QListWidget escala los iconos a 16x16 y la insignia de rango
    // queda reducida a una manchita ilegible.
    m_presenceList->setIconSize(QSize(46, 18));
    // Por pixel y no por fila: sin esto el scroll salta de item en item.
    m_presenceList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_presenceList->viewport()->installEventFilter(this);
    m_presenceList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listStack->addWidget(m_presenceList);
    connect(m_presenceList, &QListWidget::itemDoubleClicked, this, &LauncherWindow::onPresenceItemDoubleClicked);
    connect(m_presenceList, &QListWidget::customContextMenuRequested, this, &LauncherWindow::onPresenceContextMenuRequested);

    m_friendsList = new QListWidget(main);
    m_friendsList->setObjectName("friendsList");
    m_friendsList->setSpacing(4);
    m_friendsList->setIconSize(QSize(46, 18));
    m_friendsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_friendsList->viewport()->installEventFilter(this);
    m_friendsList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listStack->addWidget(m_friendsList);
    connect(m_friendsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr && !item->data(Qt::UserRole).toString().isEmpty() &&
            item->data(Qt::UserRole + 2).toBool())
            openDirectChat(item->data(Qt::UserRole).toString());
    });
    connect(m_friendsList, &QListWidget::customContextMenuRequested, this,
            &LauncherWindow::onFriendContextMenuRequested);

    mainLayout->addWidget(m_listStack, 1);
    connect(m_tabPlayersBtn, &QPushButton::clicked, this, [this]() { showListTab(false); });
    connect(m_tabFriendsBtn, &QPushButton::clicked, this, [this]() { showListTab(true); });

    auto* hint = new QLabel(QStringLiteral("Doble click para desafiar  -  click derecho: mensaje, amigo, Team, ver partida"), main);
    hint->setObjectName("hintLabel");
    mainLayout->addWidget(hint);

    root->addWidget(main, 1);
}

void LauncherWindow::applyStylesheet()
{
    setStyleSheet(R"(
        /* Antes esto era "QWidget { background: ... }", que le ponia fondo
           OPACO a cada etiqueta: sobre el degradado de la barra lateral cada
           texto se recortaba como un rectangulo. Ahora el fondo lo ponen solo
           los contenedores y las etiquetas quedan transparentes. */
        QWidget { color: #cdbe9e; font-family: "Segoe UI", sans-serif; font-size: 13px; }
        QLabel { background: transparent; }

        #sidebar {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #0d1b33, stop:1 #050d17);
            border-right: 1px solid #1c2a3f;
        }
        #mainPanel { background: #060e18; }

        #brandTitle { color: #f0e6d2; font-size: 27px; font-weight: 700; letter-spacing: 1px; }
        #brandSubtitle {
            color: #6b5a34; font-size: 10px; font-weight: 700;
            letter-spacing: 5px; padding-left: 2px;
        }
        /* Secciones normales: discretas. Las "hot" (algo espera tu accion)
           van en dorado para que salten a la vista sobre el resto. */
        #sectionLabel {
            color: #5f7a86; font-weight: 700; font-size: 11px; letter-spacing: 2px;
        }
        #sectionLabelHot {
            color: #f5c542; font-weight: 700; font-size: 11px; letter-spacing: 2px;
        }
        /* Cuadros de dialogo (sala previa, Team, chat privado, ayuda): antes
           salian con fondo gris claro y texto palido, casi ilegibles. */
        QDialog, QMessageBox { background: #0b1626; }
        QCheckBox { color: #cdbe9e; spacing: 8px; }
        QLineEdit, QTextEdit {
            background: #060e18; border: 1px solid #1c2a3f; border-radius: 6px;
            padding: 6px 8px; color: #e8e2d4; selection-background-color: #0ac8b9;
        }
        QDialog QPushButton, QMessageBox QPushButton {
            background: #0f1b2d; border: 1px solid #2a3a52; border-radius: 6px;
            padding: 8px 14px; color: #e8e2d4;
        }
        QDialog QPushButton:hover:!disabled, QMessageBox QPushButton:hover:!disabled { border-color: #0ac8b9; }
        QDialog QPushButton:disabled { color: #4a5666; border-color: #17212f; }
        QDialog QPushButton#primaryBtn { border-color: #c8aa6e; }
        QDialog QPushButton#primaryBtn:hover:!disabled { background: #c8aa6e; color: #0b1626; }
        QMenu { background: #0b1626; border: 1px solid #2a3a52; color: #e8e2d4; padding: 4px; }
        QMenu::item { padding: 6px 22px; border-radius: 4px; }
        QMenu::item:selected { background: rgba(10,200,185,0.18); }
        QMenu::item:disabled { color: #4a5666; }
        QMenu::separator { height: 1px; background: #1c2a3f; margin: 4px 8px; }
        #tabBtn {
            background: transparent; border: none; border-bottom: 2px solid transparent;
            color: #5f7a86; font-weight: 700; font-size: 11px; letter-spacing: 2px;
            padding: 4px 8px;
        }
        #tabBtn:hover { color: #0ac8b9; }
        #tabBtn:checked { color: #f0e6d2; border-bottom-color: #0ac8b9; }
        #nicknameLabel { color: #f0e6d2; font-weight: 700; font-size: 17px; }
        #statusLabel { color: #7a8894; font-size: 12px; }
        #hintLabel { color: #45525f; font-size: 11px; }

        /* Tarjeta de cuenta: agrupa avatar, nombre y estado en un bloque con
           identidad propia, en vez de tres etiquetas sueltas apiladas. */
        #accountCard {
            background: rgba(255,255,255,0.03);
            border: 1px solid #1c2a3f; border-radius: 8px;
        }
        #avatarCircle {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #0ac8b9, stop:1 #0a7d8f);
            border-radius: 21px; color: #04202a; font-size: 19px; font-weight: 700;
        }
        /* Linea fina a la derecha del titulo de seccion: separa sin gritar. */
        #sectionRule { background: #16222f; max-height: 1px; border: none; }

        QLineEdit {
            background: rgba(0,0,0,0.35); border: 1px solid #23324a; color: #f0e6d2;
            padding: 10px 12px; border-radius: 6px;
        }
        QLineEdit:focus { border: 1px solid #0ac8b9; }

        QPushButton {
            background: rgba(255,255,255,0.04); border: 1px solid #23324a; color: #cdbe9e;
            padding: 9px 16px; border-radius: 6px; font-weight: 600;
        }
        QPushButton:hover:!disabled { background: rgba(10,200,185,0.10); border-color: #0ac8b9; color: #f0e6d2; }
        QPushButton:pressed:!disabled { background: rgba(10,200,185,0.18); }
        QPushButton:disabled { color: #3c4654; border-color: #172234; background: transparent; }

        #primaryBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #1e2328, stop:1 #0a1428);
            border: 1px solid #c8aa6e; color: #f0e6d2;
        }
        #primaryBtn:hover:!disabled { background: #c8aa6e; color: #0a0f16; }
        #primaryBtn:disabled { background: transparent; }

        /* Acciones secundarias: van juntas y chicas al pie de la barra, no
           apiladas arriba compitiendo con lo que de verdad importa. */
        #ghostBtn {
            background: transparent; border: 1px solid transparent; color: #64758a;
            padding: 6px 8px; font-weight: 500; font-size: 12px; text-align: left;
        }
        #ghostBtn:hover:!disabled { color: #0ac8b9; border-color: #1c2a3f; background: rgba(255,255,255,0.03); }

        QListWidget {
            background: transparent; border: none;
            outline: none;  /* si no, la fila enfocada sale con un marco punteado feo */
        }
        /* Filas mas bajas que antes: entraban 6 jugadores en toda la lista y
           el resto era aire. Ahora entra casi el doble sin verse apretado. */
        QListWidget::item {
            background: rgba(255,255,255,0.025); border: 1px solid #172234;
            border-left: 2px solid #172234;
            border-radius: 6px; padding: 7px 12px; margin-bottom: 3px;
            color: #e8e2d4; font-size: 13px;
        }
        QListWidget::item:hover { background: rgba(10,200,185,0.07); border-color: #1f4a52; border-left-color: #0ac8b9; }
        QListWidget::item:selected { background: rgba(200,170,110,0.10); border-color: #4a3f24; border-left-color: #c8aa6e; }

        #acceptBtn { border-color: #0ac8b9; }
        #acceptBtn:hover { background: #0ac8b9; color: #010a13; }

        QScrollArea, QScrollArea > QWidget, QScrollArea > QWidget > QWidget {
            background: transparent; border: none;
        }
        #actionRow {
            background: #0b1c2c; border: 1px solid #2a2418; border-radius: 4px;
        }

        #chatLog {
            background: rgba(0,0,0,0.35); border: 1px solid #1c2a3f; border-radius: 6px;
            padding: 8px; font-size: 12px;
        }

        QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
        QScrollBar::handle:vertical { background: #24344a; border-radius: 4px; min-height: 28px; }
        QScrollBar::handle:vertical:hover { background: #0ac8b9; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )");
}

void LauncherWindow::setStatus(const QString& text)
{
    // Antes de conectar la tarjeta de cuenta esta oculta, asi que el mensaje
    // tiene que ir al que si se ve.
    m_statusLabel->setText(text);
    if (m_preLoginStatus != nullptr)
        m_preLoginStatus->setText(text);
}

void LauncherWindow::startPulse(QWidget* target)
{
    // Parented a `target`: Qt lo destruye solo cuando se destruye la fila
    // (p.ej. al reconstruir la lista con clear()), sin que haga falta
    // rastrear ni detener nada a mano.
    auto* effect = new QGraphicsOpacityEffect(target);
    target->setGraphicsEffect(effect);

    auto* up = new QPropertyAnimation(effect, "opacity");
    up->setStartValue(0.6);
    up->setEndValue(1.0);
    up->setDuration(700);
    up->setEasingCurve(QEasingCurve::InOutSine);

    auto* down = new QPropertyAnimation(effect, "opacity");
    down->setStartValue(1.0);
    down->setEndValue(0.6);
    down->setDuration(700);
    down->setEasingCurve(QEasingCurve::InOutSine);

    auto* group = new QSequentialAnimationGroup(target);
    group->addAnimation(up);
    group->addAnimation(down);
    group->setLoopCount(-1);
    group->start();
}

void LauncherWindow::addActionRow(QVBoxLayout* layout, const QString& text, const QString& textColor,
                                   std::function<void()> onAccept, std::function<void()> onDecline,
                                   bool pulse)
{
    // QListWidget::setItemWidget fallo dos veces seguidas en la prueba en
    // vivo (filas vacias) por razones que no terminamos de precisar -- esto
    // usa el camino mas simple y probado de Qt para una fila con widgets:
    // un QWidget comun metido directo en un QVBoxLayout, sin pasar por la
    // maquinaria de items/delegates de las vistas de lista.
    auto* row = new QWidget();
    row->setObjectName("actionRow");
    row->setAttribute(Qt::WA_StyledBackground, true); // si no, "background"/"border" del QSS no se pintan en un QWidget comun
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(12, 6, 10, 6);
    rowLayout->setSpacing(8);

    auto* label = new QLabel(text, row);
    label->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-weight: 700;").arg(textColor));
    label->setWordWrap(true);
    rowLayout->addWidget(label, 1);

    auto* acceptBtn = new QPushButton(QStringLiteral("Aceptar"), row);
    acceptBtn->setObjectName("acceptBtn");
    auto* declineBtn = new QPushButton(QStringLiteral("Rechazar"), row);
    rowLayout->addWidget(acceptBtn);
    rowLayout->addWidget(declineBtn);

    connect(acceptBtn, &QPushButton::clicked, this, [onAccept]() { if (onAccept) onAccept(); });
    connect(declineBtn, &QPushButton::clicked, this, [onDecline]() { if (onDecline) onDecline(); });

    // Insertar antes del stretch final (que siempre queda como ultimo item
    // del layout) para que las filas se apilen arriba.
    layout->insertWidget(layout->count() - 1, row);

    if (pulse)
        startPulse(row);
}

void LauncherWindow::addPlainRow(QVBoxLayout* layout, const QString& text, const QColor& color)
{
    auto* label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("background: transparent; color: %1;").arg(color.name()));
    layout->insertWidget(layout->count() - 1, label);
}

void LauncherWindow::clearLayout(QVBoxLayout* layout)
{
    // Deja el stretch final (el ultimo item) y borra todo lo demas.
    while (layout->count() > 1)
    {
        QLayoutItem* child = layout->takeAt(0);
        delete child->widget();
        delete child;
    }
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
    m_connectAttempts = 0;
    m_lastConnectError.clear();
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
    m_myNicknameLabel->setText(nickname);
    refreshMyLevelLabel();
    m_avatarLabel->setText(nickname.left(1).toUpper());
    m_accountCard->show();
    m_preLoginStatus->hide();
    m_codeInput->setEnabled(true);
    m_connectBtn->setEnabled(true);
    // Codigo aceptado: la caja de cuenta ya cumplio su funcion.
    m_accountBox->hide();
    QSettings("RMG-K", "n02").setValue("Launcher/PlayerCode", m_myCode);

    reportPresence();
    m_presenceTimer->start();

    if (m_friendsTimer == nullptr)
    {
        m_friendsTimer = new QTimer(this);
        m_friendsTimer->setInterval(4000);
        connect(m_friendsTimer, &QTimer::timeout, this, &LauncherWindow::onFriendsTick);
    }
    m_friendsTick = 0;
    m_friendsLoadedOnce = false;
    m_friendsTimer->start();
    m_achChecked = false;
    m_lastAchXp = -1;
    m_myProfileId.clear();
    QTimer::singleShot(4000, this, [this]() { checkAchievements(); });

    setStatus(QStringLiteral("Conectando al Lobby..."));
    m_lobbyClient->connectToServer(LOBBY_SERVER_URL, nickname, {});

    fetchFriends();
    refreshRosterDisplay();

    if (!m_pendingWatchNick.isEmpty())
    {
        const QString nick = m_pendingWatchNick;
        m_pendingWatchNick.clear();
        QTimer::singleShot(1500, this, [this, nick]() { startWatching(nick); });
    }
}

void LauncherWindow::onLobbyStateChanged(LobbyClient::ConnectionState state)
{
    using State = LobbyClient::ConnectionState;
    switch (state)
    {
    case State::Connected:
        // El nickname ya se muestra grande arriba; repetir "Conectado como X"
        // aca era puro ruido.
        setStatus(QStringLiteral("En linea"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #0ac8b9; font-weight: 600;"));
        m_lobbyConnected = true;
        m_connectAttempts = 0;
        m_lastConnectError.clear();
        break;
    case State::Connecting:
    case State::Authenticating:
        setStatus(QStringLiteral("Conectando al Lobby..."));
        m_lobbyConnected = false;
        break;
    case State::Disconnected:
        m_lobbyConnected = false;
        m_presenceList->clear();
        break;
    case State::Failed:
    {
        m_lobbyConnected = false;
        // Casi siempre es el limite de conexiones por IP del servidor
        // (429, "Retry-After: 3s"): un par de reintentos espaciados lo
        // resuelven solo. Acotado a 3 -- si no entra ahi, es otra cosa.
        if (m_connectAttempts < 3 && !m_myNickname.isEmpty())
        {
            const int delayMs = 3500 * (m_connectAttempts + 1);
            setStatus(QStringLiteral("Servidor ocupado, reintentando (%1/3)...").arg(m_connectAttempts + 1));
            const QString nickname = m_myNickname;
            QTimer::singleShot(delayMs, this, [this, nickname]() {
                if (m_lobbyClient != nullptr && !m_lobbyConnected)
                {
                    ++m_connectAttempts;
                    m_lobbyClient->connectToServer(LOBBY_SERVER_URL, nickname, {});
                }
            });
        }
        else
        {
            setStatus(m_lastConnectError.isEmpty()
                ? QStringLiteral("No se pudo conectar al Lobby.")
                : QStringLiteral("No se pudo conectar al Lobby: %1").arg(m_lastConnectError));
            m_statusLabel->setStyleSheet(QStringLiteral("color: #e06055;"));
            // Que pueda reintentar con otro codigo sin reabrir el launcher.
            m_accountBox->show();
        }
        break;
    }
    }
    m_chatInput->setEnabled(m_lobbyConnected);
    m_chatSendBtn->setEnabled(m_lobbyConnected);
}

void LauncherWindow::onLobbyHelloFailed(const QString& reason)
{
    setStatus(QStringLiteral("Error del servidor: %1").arg(reason));
}

void LauncherWindow::onLobbyConnectError(const QString& message)
{
    // Guardado para que el mensaje final (tras agotar los reintentos) diga el
    // motivo real en vez de un "no se pudo conectar" a secas.
    m_lastConnectError = message;
    setStatus(QStringLiteral("No se pudo conectar: %1").arg(message));
}

void LauncherWindow::onLobbyPresenceChanged()
{
    // m_presenceList now shows the full registered roster (see
    // refreshRosterDisplay), not just who's connected to the lobby right
    // now -- presence changes still matter, they just drive that same
    // roster's online/en-partida status instead of which rows exist.
    refreshRosterDisplay();
    refreshFriendsDisplay();
}

void LauncherWindow::fetchFullRoster()
{
    // Todos los jugadores registrados, no solo los conectados al launcher --
    // no necesita estar logueado, se pide una sola vez al abrir.
    QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/profiles?select=nickname,rank_points&order=rank_points.desc")
        .arg(SUPABASE_URL)));
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);
    m_rosterNetwork->get(req);
}

void LauncherWindow::onFullRosterReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    m_fullRoster.clear();
    for (const QJsonValue& v : doc.array())
    {
        const QJsonObject row = v.toObject();
        RosterEntry entry;
        entry.nickname = row.value("nickname").toString();
        entry.rankPoints = row.value("rank_points").toDouble();
        m_fullRoster.append(entry);
    }

    refreshRosterDisplay();
}

QString LauncherWindow::lobbyStateFor(const QString& nickname) const
{
    if (m_lobbyClient == nullptr)
        return QString();

    const auto& users = m_lobbyClient->users();
    for (auto it = users.constBegin(); it != users.constEnd(); ++it)
    {
        if (it->username == nickname)
            return it->state.isEmpty() ? QStringLiteral("idle") : it->state;
    }
    return QString();
}

void LauncherWindow::refreshRosterDisplay()
{
    refreshFriendsDisplay();
    m_presenceList->clear();

    // Sin identificarse todavia no sabemos cual nickname es "yo mismo" para
    // excluirlo de la lista -- mejor no mostrar nada hasta que conecte.
    if (m_myNickname.isEmpty())
        return;

    // Los conectados/jugando primero (siguen ordenados por rango dentro de
    // cada grupo), para que resalten sin tener que buscarlos en la lista.
    QList<const RosterEntry*> online;
    QList<const RosterEntry*> offline;
    for (const RosterEntry& entry : m_fullRoster)
    {
        if (entry.nickname == m_myNickname)
            continue; // no listarme a mi mismo
        (lobbyStateFor(entry.nickname).isEmpty() ? offline : online).append(&entry);
    }

    if (m_onlineCountLabel != nullptr)
    {
        m_onlineCountLabel->setText(online.isEmpty()
            ? QString()
            : QStringLiteral("%1 en linea").arg(online.size()));
    }

    for (const RosterEntry* entry : online + offline)
    {
        auto* item = new QListWidgetItem(m_presenceList);
        item->setData(Qt::UserRole, entry->nickname);
        const QString state = lobbyStateFor(entry->nickname);
        const bool offline = state.isEmpty();
        // El rango se guarda en la fila para que el latido pueda redibujar el
        // icono sin tener que buscar de nuevo los puntos del jugador.
        item->setData(Qt::UserRole + 1, tierForPoints(entry->rankPoints));
        // El punto de color ya dice si esta en linea; solo se escribe el
        // estado cuando aporta algo mas ("en partida").
        item->setText(QStringLiteral("%1%2%3")
            .arg(entry->nickname, levelSuffix(entry->nickname),
                 state == QStringLiteral("playing") ? QStringLiteral("   en partida") : QString()));
        item->setIcon(rosterIcon(dotColorForState(state), tierForPoints(entry->rankPoints), offline));
        if (offline)
            item->setForeground(QColor(0x54, 0x60, 0x6e));
    }
}

void LauncherWindow::fetchFriends()
{
    if (m_myCode.isEmpty())
        return;
    QJsonObject body;
    body["p_my_code"] = m_myCode;
    postFriendsRpc(QStringLiteral("list_friends"), body, "list");
}

void LauncherWindow::onListFriendsReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
    {
        // Un fallo suelto de red no debe borrar la lista ni asustar: se
        // conserva lo que habia y solo se avisa si se repite.
        if (++m_friendsErrors >= 3)
            setStatus(QStringLiteral("No se pudo actualizar la lista de amigos. Reintentando..."));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
    {
        ++m_friendsErrors;
        return;
    }
    m_friendsErrors = 0;

    QList<FriendEntry> fresh;
    for (const QJsonValue& v : doc.array())
    {
        const QJsonObject row = v.toObject();
        FriendEntry entry;
        entry.nickname = row.value("nickname").toString();
        entry.status = row.value("status").toString();
        entry.iAmRequester = row.value("i_am_requester").toBool();
        fresh.append(entry);
    }

    // Avisos: solicitud nueva que me llego, o solicitud mia que aceptaron.
    QSet<QString> incoming;
    QSet<QString> outgoing;
    QSet<QString> acceptedNow;
    for (const FriendEntry& e : fresh)
    {
        if (e.status == QStringLiteral("accepted"))
            acceptedNow.insert(e.nickname);
        else if (e.iAmRequester)
            outgoing.insert(e.nickname);
        else
            incoming.insert(e.nickname);
    }
    if (m_friendsLoadedOnce)
    {
        for (const QString& nick : incoming)
            if (!m_knownIncomingRequests.contains(nick))
                notifyToast(QStringLiteral("Solicitud de amistad"),
                            QStringLiteral("%1 quiere ser tu amigo.").arg(nick));
        for (const QString& nick : m_knownOutgoing)
            if (acceptedNow.contains(nick))
                notifyToast(QStringLiteral("Solicitud aceptada"),
                            QStringLiteral("%1 acepto tu solicitud de amistad.").arg(nick));
    }
    m_knownIncomingRequests = incoming;
    m_knownOutgoing = outgoing;
    m_friendsLoadedOnce = true;

    m_friends = fresh;
    refreshFriendsDisplay();
}

void LauncherWindow::onSendFriendRequestReply(QNetworkReply* reply)
{
    reply->deleteLater();
    const QString target = reply->property("target").toString();
    const QByteArray raw = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
    {
        const QJsonDocument err = QJsonDocument::fromJson(raw);
        setStatus(QStringLiteral("No se pudo agregar: %1")
            .arg(err.object().value("message").toString(QStringLiteral("error desconocido"))));
        return;
    }
    const bool nowFriends = raw.contains("aceptada");
    setStatus(nowFriends ? QStringLiteral("%1 y vos ya son amigos.").arg(target)
                         : QStringLiteral("Solicitud de amistad enviada a %1.").arg(target));
    notifyToast(nowFriends ? QStringLiteral("Ya son amigos") : QStringLiteral("Solicitud enviada"),
                nowFriends ? QStringLiteral("%1 ya es tu amigo.").arg(target)
                           : QStringLiteral("Le avisamos a %1. Te avisamos cuando responda.").arg(target));
    fetchFriends();
}

void LauncherWindow::showListTab(bool friends)
{
    if (m_listStack == nullptr)
        return;
    m_listStack->setCurrentIndex(friends ? 1 : 0);
    m_tabPlayersBtn->setChecked(!friends);
    m_tabFriendsBtn->setChecked(friends);
    if (friends)
        fetchFriends(); // al abrir la pestaña siempre se ve lo ultimo
}

void LauncherWindow::refreshFriendsDisplay()
{
    if (m_friendsList == nullptr)
        return;

    m_friendsList->clear();
    clearLayout(m_friendRequestsLayout);

    struct Row { QString nickname; QString state; };
    QList<Row> accepted;
    QStringList outgoing;
    int requestCount = 0;
    for (const FriendEntry& friendEntry : m_friends)
    {
        if (friendEntry.status == QStringLiteral("accepted"))
        {
            accepted.append({friendEntry.nickname, lobbyStateFor(friendEntry.nickname)});
            continue;
        }
        if (friendEntry.iAmRequester)
        {
            outgoing.append(friendEntry.nickname);
            continue;
        }

        // Me llego una solicitud: va al buzon aparte, con Aceptar/Rechazar.
        const QString nickname = friendEntry.nickname;
        ++requestCount;
        addActionRow(m_friendRequestsLayout, QStringLiteral("%1 quiere ser tu amigo").arg(nickname), "#f5c542",
                     [this, nickname]() { respondFriendRequest(nickname, true); },
                     [this, nickname]() { respondFriendRequest(nickname, false); },
                     /*pulse=*/true);
    }

    // Conectados primero, despues por orden alfabetico.
    std::sort(accepted.begin(), accepted.end(), [](const Row& a, const Row& b) {
        const bool ao = !a.state.isEmpty(), bo = !b.state.isEmpty();
        if (ao != bo)
            return ao;
        return a.nickname.compare(b.nickname, Qt::CaseInsensitive) < 0;
    });

    int online = 0;
    int unreadTotal = 0;
    for (const Row& row : accepted)
    {
        double points = 0;
        for (const RosterEntry& r : m_fullRoster)
            if (r.nickname == row.nickname)
                points = r.rankPoints;

        const bool offline = row.state.isEmpty();
        if (!offline)
            ++online;
        const int unread = m_dmUnread.value(row.nickname, 0);
        unreadTotal += unread;

        QString text = row.nickname + levelSuffix(row.nickname);
        if (row.state == QStringLiteral("playing"))
            text += QStringLiteral("   en partida");
        else if (offline)
            text += QStringLiteral("   desconectado");
        if (unread > 0)
            text += QStringLiteral("   \u2709 %1 %2").arg(unread).arg(unread == 1 ? QStringLiteral("nuevo") : QStringLiteral("nuevos"));

        auto* item = new QListWidgetItem(m_friendsList);
        item->setData(Qt::UserRole, row.nickname);
        item->setData(Qt::UserRole + 2, true); // amigo real (se puede chatear)
        item->setText(text);
        item->setIcon(rosterIcon(dotColorForState(row.state), tierForPoints(points), offline));
        if (unread > 0)
            item->setForeground(QColor(0xf5, 0xc5, 0x42));
        else if (offline)
            item->setForeground(QColor(0x5a, 0x55, 0x48));
    }

    for (const QString& nickname : outgoing)
    {
        auto* item = new QListWidgetItem(m_friendsList);
        item->setData(Qt::UserRole, nickname);
        item->setText(QStringLiteral("%1   esperando respuesta").arg(nickname));
        item->setForeground(QColor(0x78, 0x5a, 0x28));
    }

    if (m_friendsList->count() == 0)
    {
        auto* empty = new QListWidgetItem(
            QStringLiteral("Todavia no tenes amigos. Click derecho a un jugador y elegi \"Agregar de amigo\"."),
            m_friendsList);
        empty->setFlags(Qt::NoItemFlags);
    }

    if (m_tabFriendsBtn != nullptr)
    {
        QString label = QStringLiteral("AMIGOS");
        if (!accepted.isEmpty())
            label += QStringLiteral("  %1/%2").arg(online).arg(accepted.size());
        if (unreadTotal > 0)
            label += QStringLiteral("  \u2709");
        m_tabFriendsBtn->setText(label);
    }

    m_friendRequestsSection->setVisible(requestCount > 0);
    if (requestCount > 0)
        fitScrollHeight(m_friendRequestsScroll, requestCount);
}

bool LauncherWindow::isBroadcastingPlayer(const QString& nickname) const
{
    if (m_lobbyClient == nullptr)
        return false;
    for (auto it = m_lobbyClient->rooms().constBegin(); it != m_lobbyClient->rooms().constEnd(); ++it)
    {
        if (it->state != QStringLiteral("in_game") || !it->broadcasting || it->matchId == 0)
            continue;
        for (const QString& n : it->playerNames)
            if (n.compare(nickname, Qt::CaseInsensitive) == 0)
                return true;
    }
    return false;
}

void LauncherWindow::onFriendContextMenuRequested(const QPoint& pos)
{
    QListWidgetItem* item = m_friendsList->itemAt(pos);
    if (item == nullptr || m_myCode.isEmpty() || item->data(Qt::UserRole).toString().isEmpty())
        return;

    const QString nickname = item->data(Qt::UserRole).toString();
    const bool isFriend = item->data(Qt::UserRole + 2).toBool();

    QMenu menu(m_friendsList);
    QAction* dmAction = isFriend ? menu.addAction(QStringLiteral("Mensaje privado a %1").arg(nickname)) : nullptr;
    if (dmAction != nullptr)
        menu.addSeparator();
    QAction* rankedAction = menu.addAction(QStringLiteral("Desafiar a %1 (Clasificatoria)").arg(nickname));
    QAction* casualAction = menu.addAction(QStringLiteral("Desafiar a %1 (Amistosa)").arg(nickname));
    QAction* teamAction = nullptr;
    if (!m_teamActive)
        teamAction = menu.addAction(QStringLiteral("Invitar a %1 a Team").arg(nickname));
    QAction* watchAction = isBroadcastingPlayer(nickname)
        ? menu.addAction(QStringLiteral("Ver la partida de %1 en vivo").arg(nickname)) : nullptr;

    QAction* chosen = menu.exec(m_friendsList->mapToGlobal(pos));
    if (chosen == nullptr)
        return;
    if (chosen == dmAction)
        openDirectChat(nickname);
    else if (chosen == rankedAction)
        challengePlayer(nickname, QStringLiteral("ranked"));
    else if (chosen == casualAction)
        challengePlayer(nickname, QStringLiteral("casual"));
    else if (chosen == teamAction)
        inviteToTeam(nickname);
    else if (chosen == watchAction)
        startWatching(nickname);
}

void LauncherWindow::openDirectChat(const QString& nickname)
{
    if (nickname.isEmpty() || m_myCode.isEmpty())
        return;

    if (m_dmDialogs.contains(nickname) && !m_dmDialogs.value(nickname).isNull())
    {
        QDialog* existing = m_dmDialogs.value(nickname);
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("Chat con %1").arg(nickname));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(420, 480);

    auto* layout = new QVBoxLayout(dialog);
    auto* header = new QLabel(QStringLiteral("Chat privado con %1").arg(nickname), dialog);
    header->setStyleSheet(QStringLiteral("font-weight:700; font-size:14px;"));
    layout->addWidget(header);

    auto* log = new QTextEdit(dialog);
    log->setObjectName("chatLog");
    log->setReadOnly(true);
    layout->addWidget(log, 1);

    auto* row = new QHBoxLayout();
    auto* input = new QLineEdit(dialog);
    input->setPlaceholderText(QStringLiteral("Escribi un mensaje..."));
    input->setMaxLength(500);
    auto* send = new QPushButton(QStringLiteral("Enviar"), dialog);
    row->addWidget(input, 1);
    row->addWidget(send);
    layout->addLayout(row);

    auto doSend = [this, nickname, input]() {
        const QString text = input->text().trimmed();
        if (text.isEmpty())
            return;
        input->clear();
        sendDm(nickname, text);
    };
    connect(send, &QPushButton::clicked, dialog, doSend);
    connect(input, &QLineEdit::returnPressed, dialog, doSend);
    connect(dialog, &QObject::destroyed, this, [this, nickname]() {
        m_dmDialogs.remove(nickname);
        m_dmLogs.remove(nickname);
    });

    m_dmDialogs.insert(nickname, dialog);
    m_dmLogs.insert(nickname, log);
    m_dmLastId.insert(nickname, 0);
    m_dmUnread.remove(nickname);
    refreshFriendsDisplay();

    dialog->show();
    input->setFocus();
    fetchDm(nickname);
}

void LauncherWindow::fetchDm(const QString& nickname)
{
    QJsonObject body;
    body["p_my_code"] = m_myCode;
    body["p_with"] = nickname;
    body["p_after"] = static_cast<double>(m_dmLastId.value(nickname, 0));
    QNetworkReply* reply = postFriendsRpc(QStringLiteral("list_dm"), body, "dm_list");
    reply->setProperty("nick", nickname);
}

void LauncherWindow::sendDm(const QString& nickname, const QString& text)
{
    QJsonObject body;
    body["p_my_code"] = m_myCode;
    body["p_to"] = nickname;
    body["p_body"] = text;
    QNetworkReply* reply = postFriendsRpc(QStringLiteral("send_dm"), body, "dm_send");
    reply->setProperty("nick", nickname);
}

void LauncherWindow::fetchUnreadDms()
{
    QJsonObject body;
    body["p_my_code"] = m_myCode;
    postFriendsRpc(QStringLiteral("dm_unread"), body, "dm_unread");
}

void LauncherWindow::onDmListReply(QNetworkReply* reply)
{
    reply->deleteLater();
    const QString nick = reply->property("nick").toString();
    if (reply->error() != QNetworkReply::NoError)
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QTextEdit* log = m_dmLogs.value(nick, nullptr);
    if (!doc.isArray() || log == nullptr)
        return;

    // El servidor las entrega de la mas nueva a la mas vieja.
    QList<QJsonObject> rows;
    for (const QJsonValue& v : doc.array())
        rows.prepend(v.toObject());

    bool incomingNew = false;
    for (const QJsonObject& row : rows)
    {
        const qint64 id = static_cast<qint64>(row.value("id").toDouble());
        if (id <= m_dmLastId.value(nick, 0))
            continue;
        m_dmLastId[nick] = id;

        const QString from = row.value("from_nickname").toString();
        const bool mine = (from == m_myNickname);
        const QDateTime when = QDateTime::fromString(row.value("created_at").toString(), Qt::ISODate).toLocalTime();
        log->append(QStringLiteral("<span style='color:#5a5548;'>%1</span> <b style='color:%2;'>%3:</b> %4")
            .arg(when.isValid() ? when.toString(QStringLiteral("HH:mm")) : QString(),
                 mine ? QStringLiteral("#c8aa6e") : QStringLiteral("#0ac8b9"),
                 mine ? QStringLiteral("Vos") : from.toHtmlEscaped(),
                 row.value("body").toString().toHtmlEscaped()));
        if (!mine)
            incomingNew = true;
    }
    if (QScrollBar* bar = log->verticalScrollBar())
        bar->setValue(bar->maximum());

    QDialog* dlg = m_dmDialogs.value(nick);
    if (incomingNew && dlg != nullptr && !dlg->isActiveWindow())
        notifyToast(QStringLiteral("Mensaje de %1").arg(nick), QStringLiteral("Tenes un mensaje nuevo."));
}

void LauncherWindow::onDmSendReply(QNetworkReply* reply)
{
    reply->deleteLater();
    const QString nick = reply->property("nick").toString();
    const QByteArray raw = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
    {
        const QString msg = QJsonDocument::fromJson(raw).object().value("message")
            .toString(QStringLiteral("no se pudo enviar"));
        if (QTextEdit* log = m_dmLogs.value(nick, nullptr))
            log->append(QStringLiteral("<span style='color:#e06055;'>No se envio: %1</span>").arg(msg.toHtmlEscaped()));
        return;
    }
    fetchDm(nick);
}

void LauncherWindow::onDmUnreadReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    QHash<QString, int> unread;
    for (const QJsonValue& v : doc.array())
    {
        const QJsonObject row = v.toObject();
        const QString from = row.value("from_nickname").toString();
        const qint64 lastId = static_cast<qint64>(row.value("last_id").toDouble());

        // Ventana abierta: se trae el mensaje directo, no cuenta como "sin leer".
        if (m_dmDialogs.contains(from) && !m_dmDialogs.value(from).isNull())
        {
            fetchDm(from);
            continue;
        }
        unread.insert(from, static_cast<int>(row.value("cnt").toDouble()));
        if (lastId > m_dmNotifiedId.value(from, 0))
        {
            m_dmNotifiedId[from] = lastId;
            notifyToast(QStringLiteral("Mensaje de %1").arg(from), row.value("last_body").toString().left(120));
        }
    }
    if (unread != m_dmUnread)
    {
        m_dmUnread = unread;
        refreshFriendsDisplay();
    }
}

void LauncherWindow::checkAchievements()
{
    if (m_myNickname.isEmpty() || m_achBusy)
        return;
    m_achBusy = true;

    // Primero el id del perfil (la lectura publica del ranking lo permite),
    // despues todas sus partidas en orden cronologico.
    if (m_myProfileId.isEmpty())
    {
        QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/profiles?select=id&nickname=eq.%2")
            .arg(SUPABASE_URL, QString::fromUtf8(QUrl::toPercentEncoding(m_myNickname)))));
        req.setRawHeader("apikey", SUPABASE_ANON_KEY);
        req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);
        req.setTransferTimeout(10000);
        QNetworkReply* reply = m_friendsNetwork->get(req);
        reply->setProperty("op", "ach_profile");
        return;
    }

    QNetworkRequest req(QUrl(QStringLiteral(
        "%1/rest/v1/match_players?select=result,character_id,max_combo_dealt,matches(played_at)"
        "&player_id=eq.%2&result=not.is.null&order=matches(played_at).asc&limit=5000")
        .arg(SUPABASE_URL, m_myProfileId)));
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_friendsNetwork->get(req);
    reply->setProperty("op", "ach_matches");
}

void LauncherWindow::onAchProfileReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
    {
        m_achBusy = false;
        return;
    }
    const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
    if (arr.isEmpty())
    {
        m_achBusy = false;
        return;
    }
    m_myProfileId = arr.at(0).toObject().value("id").toString();
    m_achBusy = false;
    checkAchievements();
}

void LauncherWindow::onAchMatchesReply(QNetworkReply* reply)
{
    reply->deleteLater();
    m_achBusy = false;
    if (reply->error() != QNetworkReply::NoError || m_myNickname.isEmpty())
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    QList<Achievements::Match> matches;
    for (const QJsonValue& v : doc.array())
    {
        const QJsonObject o = v.toObject();
        Achievements::Match m;
        m.playedMs = QDateTime::fromString(o.value("matches").toObject().value("played_at").toString(), Qt::ISODate)
                         .toMSecsSinceEpoch();
        m.win = (o.value("result").toString() == QStringLiteral("win"));
        m.characterId = o.value("character_id").isNull() ? -1 : o.value("character_id").toInt();
        m.combo = o.value("max_combo_dealt").isNull() ? 0 : o.value("max_combo_dealt").toInt();
        matches.append(m);
    }

    QSettings settings("RMG-K", "n02");
    const QString base = QStringLiteral("Launcher/Ach/%1/").arg(m_myNickname);
    const bool seed = !settings.contains(base + "LastMs");
    const QStringList unlocked = settings.value(base + "Unlocked").toStringList();
    const qint64 lastMs = settings.value(base + "LastMs", 0).toLongLong();

    const Achievements::Result result = Achievements::evaluate(matches, unlocked, lastMs, seed);

    settings.setValue(base + "Unlocked", result.unlocked);
    settings.setValue(base + "LastMs", QString::number(qMax(result.lastMs, lastMs)));
    m_achChecked = true;

    if (result.events.isEmpty())
        return;

    // Cada novedad queda en el chat del launcher (registro) y se avisa con
    // UNA sola notificacion, para que no se pisen unas a otras.
    QStringList lines;
    for (const Achievements::Event& e : result.events)
    {
        appendChatLine(QStringLiteral("Logro"), QStringLiteral("%1 - %2").arg(e.title, e.text), true);
        if (lines.size() < 4)
            lines << QStringLiteral("%1: %2").arg(e.title, e.text);
    }
    const QString title = (result.events.size() == 1) ? result.events.first().title
                                                      : QStringLiteral("%1 novedades").arg(result.events.size());
    QString body = (result.events.size() == 1) ? result.events.first().text : lines.join(QStringLiteral("\n"));
    if (result.events.size() > lines.size())
        body += QStringLiteral("\n... y %1 mas").arg(result.events.size() - lines.size());
    notifyToast(title, body);
}

void LauncherWindow::fetchLevels()
{
    postFriendsRpc(QStringLiteral("player_stats_all"), QJsonObject(), "levels");
}

void LauncherWindow::onLevelsReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray())
        return;

    QHash<QString, qint64> fresh;
    for (const QJsonValue& v : doc.array())
    {
        const QJsonObject row = v.toObject();
        fresh.insert(row.value("nickname").toString(),
                     static_cast<qint64>(row.value("games").toDouble()) * 100 +
                     static_cast<qint64>(row.value("wins").toDouble()) * 50);
    }
    if (fresh == m_xp)
        return;

    const int before = m_myNickname.isEmpty() ? 0 : levelFromXp(m_xp.value(m_myNickname, 0));
    m_xp = fresh;
    refreshRosterDisplay();
    refreshMyLevelLabel();

    if (!m_myNickname.isEmpty())
    {
        const qint64 myXpNow = m_xp.value(m_myNickname, 0);
        if (!m_achChecked || myXpNow != m_lastAchXp)
        {
            m_lastAchXp = myXpNow;
            checkAchievements();
        }
    }

    // Aviso de subida de nivel (solo si ya conocia mi nivel de antes).
    if (before > 0 && !m_myNickname.isEmpty())
    {
        const int after = levelFromXp(m_xp.value(m_myNickname, 0));
        if (after > before)
            notifyToast(QStringLiteral("Subiste de nivel!"), QStringLiteral("Ahora sos nivel %1.").arg(after));
    }
}

QString LauncherWindow::levelSuffix(const QString& nickname) const
{
    if (!m_xp.contains(nickname))
        return QString();
    return QStringLiteral("   Nv %1").arg(levelFromXp(m_xp.value(nickname)));
}

void LauncherWindow::refreshMyLevelLabel()
{
    if (m_myNicknameLabel == nullptr || m_myNickname.isEmpty())
        return;
    if (!m_xp.contains(m_myNickname))
    {
        m_myNicknameLabel->setText(m_myNickname);
        return;
    }
    const qint64 xp = m_xp.value(m_myNickname);
    const int level = levelFromXp(xp);
    const qint64 current = xp - xpTotalForLevel(level);
    const qint64 need = xpTotalForLevel(level + 1) - xpTotalForLevel(level);
    m_myNicknameLabel->setText(QStringLiteral("%1  <span style='color:#c8aa6e; font-size:11px; font-weight:600;'>Nv %2</span>")
        .arg(m_myNickname.toHtmlEscaped()).arg(level));
    m_myNicknameLabel->setToolTip(QStringLiteral("Nivel %1  -  %2/%3 XP para el siguiente\n%4 XP en total")
        .arg(level).arg(current).arg(need).arg(xp));
}

void LauncherWindow::setPresenceMode(const QString& mode)
{
    m_presenceMode = mode;
    reportPresence();
    if (mode == QStringLiteral("dnd"))
        setStatus(QStringLiteral("No molestar: sin sonidos ni avisos."));
    else if (mode == QStringLiteral("online"))
        setStatus(QStringLiteral("En linea"));
}

double LauncherWindow::rankPointsFor(const QString& nickname) const
{
    for (const RosterEntry& r : m_fullRoster)
        if (r.nickname == nickname)
            return r.rankPoints;
    return 0;
}

void LauncherWindow::setSearching(bool on)
{
    if (on == m_searching)
        return;
    m_searching = on;

    if (on)
    {
        m_searchStartMs = QDateTime::currentMSecsSinceEpoch();
        m_searchers.clear();
        m_mmChallenged.clear();
        m_searchTickCount = 0;
        m_modeBeforeSearch = m_presenceMode;
        if (m_presenceMode != QStringLiteral("dnd"))
            setPresenceMode(QStringLiteral("searching"));
        if (m_searchTimer == nullptr)
        {
            m_searchTimer = new QTimer(this);
            m_searchTimer->setInterval(3000);
            connect(m_searchTimer, &QTimer::timeout, this, &LauncherWindow::searchTick);
        }
        m_searchTimer->start();
        if (m_searchBtn != nullptr)
            m_searchBtn->setText(QStringLiteral("  Cancelar busqueda"));
        setStatus(QStringLiteral("Buscando rival de tu nivel..."));
        searchTick();
    }
    else
    {
        if (m_searchTimer != nullptr)
            m_searchTimer->stop();
        if (m_lobbyClient != nullptr && m_lobbyConnected && !m_myNickname.isEmpty())
            m_lobbyClient->sendChat(QStringLiteral("lobby"), QStringLiteral("__MM_OFF__|%1").arg(m_myNickname));
        if (m_presenceMode == QStringLiteral("searching"))
            setPresenceMode(m_modeBeforeSearch == QStringLiteral("dnd") ? QStringLiteral("dnd") : QStringLiteral("online"));
        m_searchers.clear();
        m_mmChallenged.clear();
        if (m_searchBtn != nullptr)
            m_searchBtn->setText(QStringLiteral("  Buscar partida"));
    }
}

void LauncherWindow::searchTick()
{
    if (!m_searching || m_lobbyClient == nullptr || !m_lobbyConnected)
    {
        if (m_searching && !m_lobbyConnected)
            setSearching(false);
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Me anuncio cada ~6 s (cada 2 ticks) con mi puntaje.
    if ((m_searchTickCount++ % 2) == 0)
    {
        m_lobbyClient->sendChat(QStringLiteral("lobby"),
            QStringLiteral("__MM__|%1|%2").arg(m_myNickname).arg(static_cast<int>(rankPointsFor(m_myNickname))));
    }

    for (auto it = m_searchers.begin(); it != m_searchers.end();)
    {
        if (now - it.value().lastSeenMs > 20000)
            it = m_searchers.erase(it);
        else
            ++it;
    }
    if (!m_mmChallenged.isEmpty() && now - m_mmChallengeMs > 20000)
        m_mmChallenged.clear();

    // El rango de puntos que acepto se va abriendo: +60 cada 15 s buscando.
    const int elapsed = static_cast<int>((now - m_searchStartMs) / 1000);
    const double window = 100 + 60 * (elapsed / 15);
    const double mine = rankPointsFor(m_myNickname);

    QString best;
    double bestDiff = 1e12;
    for (auto it = m_searchers.constBegin(); it != m_searchers.constEnd(); ++it)
    {
        const QString state = lobbyStateFor(it.key());
        if (state.isEmpty() || state == QStringLiteral("playing"))
            continue;
        const double diff = qAbs(it.value().points - mine);
        if (diff <= window && diff < bestDiff)
        {
            best = it.key();
            bestDiff = diff;
        }
    }

    // Solo uno de los dos reta (el de nickname menor en orden alfabetico);
    // el otro acepta solo. Asi no se cruzan dos desafios.
    if (!best.isEmpty() && m_mmChallenged.isEmpty() &&
        m_myNickname.compare(best, Qt::CaseInsensitive) < 0)
    {
        m_mmChallenged = best;
        m_mmChallengeMs = now;
        setStatus(QStringLiteral("Rival encontrado: %1").arg(best));
        challengePlayer(best, QStringLiteral("ranked"));
    }
}

void LauncherWindow::offerRematch(const QString& opponent, const QString& matchType)
{
    if (opponent.isEmpty() || m_rematchBtn == nullptr)
        return;
    m_rematchOpponent = opponent;
    m_rematchType = matchType.isEmpty() ? QStringLiteral("ranked") : matchType;
    m_rematchBtn->setText(QStringLiteral("  Revancha con %1").arg(opponent));
    m_rematchBtn->show();
    notifyToast(QStringLiteral("Revancha"), QStringLiteral("Podes pedirle la revancha a %1.").arg(opponent));
    QTimer::singleShot(5 * 60 * 1000, this, [this, opponent]() {
        if (m_rematchOpponent == opponent && m_rematchBtn != nullptr)
        {
            m_rematchBtn->hide();
            m_rematchOpponent.clear();
        }
    });
}

void LauncherWindow::fetchHeadToHead(const QString& nickname)
{
    if (m_myNickname.isEmpty() || nickname.isEmpty())
        return;
    QJsonObject body;
    body["p_a"] = m_myNickname;
    body["p_b"] = nickname;
    QNetworkReply* reply = postFriendsRpc(QStringLiteral("head_to_head"), body, "h2h");
    reply->setProperty("nick", nickname);
}

void LauncherWindow::onH2hReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;
    const QString nick = reply->property("nick").toString();
    const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
    const int a = o.value("a_wins").toInt();
    const int b = o.value("b_wins").toInt();
    m_h2hCache[nick] = (a + b) > 0 ? QStringLiteral("cara a cara: vos %1 - %2").arg(a).arg(b)
                                   : QStringLiteral("primer enfrentamiento");
    refreshPendingChallengesDisplay();
}

void LauncherWindow::showDiagnostics()
{
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("Diagnostico de conexion"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(420, 260);
    auto* layout = new QVBoxLayout(dialog);
    auto* title = new QLabel(QStringLiteral("Diagnostico de conexion"), dialog);
    title->setStyleSheet(QStringLiteral("font-weight:700; font-size:15px;"));
    layout->addWidget(title);
    auto* result = new QLabel(QStringLiteral("Midiendo..."), dialog);
    result->setWordWrap(true);
    result->setTextFormat(Qt::RichText);
    layout->addWidget(result, 1);
    auto* close = new QPushButton(QStringLiteral("Cerrar"), dialog);
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    dialog->show();

    // 1) Ida y vuelta al servidor del Lobby (conexion TCP, promedio de 3).
    QTimer::singleShot(50, dialog, [this, dialog, result]() {
        const QUrl lobbyUrl(QString::fromUtf8(LOBBY_SERVER_URL));
        qint64 total = 0;
        int ok = 0;
        for (int i = 0; i < 3; ++i)
        {
            QTcpSocket socket;
            QElapsedTimer timer;
            timer.start();
            socket.connectToHost(lobbyUrl.host(), static_cast<quint16>(lobbyUrl.port(8080)));
            if (socket.waitForConnected(3000))
            {
                total += timer.elapsed();
                ++ok;
                socket.disconnectFromHost();
            }
        }
        const int lobbyMs = ok > 0 ? static_cast<int>(total / ok) : -1;

        // 2) Tiempo de respuesta de la base de datos (Supabase).
        auto* net = new QNetworkAccessManager(dialog);
        QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/live_state?select=id&limit=1").arg(SUPABASE_URL)));
        req.setRawHeader("apikey", SUPABASE_ANON_KEY);
        req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);
        req.setTransferTimeout(6000);
        auto* clock = new QElapsedTimer();
        clock->start();
        QNetworkReply* reply = net->get(req);
        connect(reply, &QNetworkReply::finished, dialog, [reply, clock, result, lobbyMs]() {
            const bool dbOk = reply->error() == QNetworkReply::NoError;
            const qint64 dbMs = clock->elapsed();
            delete clock;
            reply->deleteLater();

            QString quality;
            QString delayAdvice;
            if (lobbyMs < 0)
            {
                quality = QStringLiteral("<span style='color:#e06055;'>No se pudo llegar al servidor del Lobby.</span>");
            }
            else
            {
                if (lobbyMs < 40)       quality = QStringLiteral("<span style='color:#0ac8b9;'>Excelente</span>");
                else if (lobbyMs < 80)  quality = QStringLiteral("<span style='color:#0ac8b9;'>Buena</span>");
                else if (lobbyMs < 130) quality = QStringLiteral("<span style='color:#f5c542;'>Aceptable</span>");
                else                    quality = QStringLiteral("<span style='color:#e06055;'>Alta: puede haber lag</span>");
                // Un cuadro dura 16.7 ms y la ida es la mitad del ida y vuelta.
                const int delay = qBound(1, static_cast<int>(std::ceil(lobbyMs / 33.4)) + 1, 8);
                delayAdvice = QStringLiteral("<br>Delay sugerido en partida: <b>%1</b> cuadros (dejalo en Auto si dudas).").arg(delay);
            }
            result->setText(QStringLiteral(
                "Servidor del Lobby: <b>%1</b><br>Base de datos (ranking): <b>%2</b><br><br>"
                "Calidad de tu conexion: %3%4<br><br>"
                "<span style='color:#5a5548;'>Es la latencia hacia los servidores; contra un rival "
                "influye tambien su propia conexion (el ping real se ve en la sala previa).</span>")
                .arg(lobbyMs >= 0 ? QStringLiteral("%1 ms").arg(lobbyMs) : QStringLiteral("sin respuesta"),
                     dbOk ? QStringLiteral("%1 ms").arg(dbMs) : QStringLiteral("sin respuesta"),
                     quality, delayAdvice));
        });
    });
}

void LauncherWindow::showPendingUpdateNotes()
{
    QSettings s("RMG-K", "n02");
    const QString version = s.value("Launcher/PendingNotesVersion").toString();
    if (version.isEmpty())
        return;
    const QString notes = s.value("Launcher/PendingNotes").toString();
    s.remove("Launcher/PendingNotesVersion");
    s.remove("Launcher/PendingNotes");
    QMessageBox::information(this, QStringLiteral("Prince actualizado"),
        QStringLiteral("Ahora tenes la version %1.\n\nNovedades:\n%2")
            .arg(version, notes.isEmpty() ? QStringLiteral("Mejoras y correcciones.") : notes));
}

void LauncherWindow::showWelcomeIfFirstRun()
{
    QSettings s("RMG-K", "n02");
    if (s.value("Launcher/WizardDone", false).toBool() ||
        !s.value("Launcher/PlayerCode").toString().isEmpty())
        return; // ya tiene cuenta o ya vio la bienvenida

    QFileInfo rom(romFilePath());
    const bool romOk = rom.exists() && rom.size() > 1000000;

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("Bienvenido a Prince"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(460, 330);
    auto* layout = new QVBoxLayout(dialog);
    auto* title = new QLabel(QStringLiteral("Bienvenido a Smash Remix Online"), dialog);
    title->setStyleSheet(QStringLiteral("font-weight:700; font-size:16px;"));
    layout->addWidget(title);

    auto* steps = new QLabel(dialog);
    steps->setWordWrap(true);
    steps->setTextFormat(Qt::RichText);
    steps->setText(QStringLiteral(
        "<b>1.</b> La ROM del juego: %1<br><br>"
        "<b>2.</b> Configura tus controles (boton de abajo).<br><br>"
        "<b>3.</b> Registrate en la pagina web para obtener tu <b>codigo de jugador</b>, "
        "y escribilo arriba a la izquierda en Prince. Solo hace falta una vez.<br><br>"
        "<b>4.</b> Desafia a alguien con doble click, o toca <b>Buscar partida</b>.")
        .arg(romOk ? QStringLiteral("<span style='color:#0ac8b9;'>encontrada</span>")
                   : QStringLiteral("<span style='color:#e06055;'>no encontrada junto al launcher</span>")));
    layout->addWidget(steps, 1);

    auto* controls = new QPushButton(QStringLiteral("Configurar controles"), dialog);
    layout->addWidget(controls);
    connect(controls, &QPushButton::clicked, dialog, [this]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        QProcess::startDetached(exeDir + QStringLiteral("/RMG-K.exe"),
                                {QStringLiteral("--open-settings=input")}, exeDir);
    });
    auto* web = new QPushButton(QStringLiteral("Abrir la pagina web"), dialog);
    layout->addWidget(web);
    connect(web, &QPushButton::clicked, dialog, [this]() { onWebsiteButtonClicked(); });
    auto* done = new QPushButton(QStringLiteral("Entendido"), dialog);
    done->setObjectName("primaryBtn");
    layout->addWidget(done);
    connect(done, &QPushButton::clicked, dialog, [dialog]() {
        QSettings("RMG-K", "n02").setValue("Launcher/WizardDone", true);
        dialog->close();
    });
    dialog->show();
}

void LauncherWindow::notifyToast(const QString& title, const QString& text)
{
    if (m_presenceMode == QStringLiteral("dnd"))
        return;
    PlaySoundW(L"Notification.Default", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
    if (m_trayIcon != nullptr)
        m_trayIcon->showMessage(title, text, QSystemTrayIcon::Information, 6000);
    QApplication::alert(this);
}

QNetworkReply* LauncherWindow::postFriendsRpc(const QString& function, const QJsonObject& body, const char* op)
{
    QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/rpc/%2").arg(SUPABASE_URL, function)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);
    req.setTransferTimeout(10000);
    QNetworkReply* reply = m_friendsNetwork->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setProperty("op", op);
    return reply;
}

void LauncherWindow::onFriendsTick()
{
    if (m_myCode.isEmpty())
        return;
    // Cada 4 s: mensajes de las ventanas abiertas. Cada 8 s: mensajes nuevos.
    // Cada 16 s: lista de amigos y solicitudes.
    for (auto it = m_dmDialogs.constBegin(); it != m_dmDialogs.constEnd(); ++it)
        if (!it.value().isNull())
            fetchDm(it.key());
    if ((m_friendsTick % 2) == 0)
        fetchUnreadDms();
    if ((m_friendsTick % 4) == 0)
        fetchFriends();
    // Cada ~60 s: puntos de todos y niveles (antes el ranking se pedia una sola vez).
    if ((m_friendsTick % 15) == 0)
    {
        fetchFullRoster();
        fetchLevels();
    }
    ++m_friendsTick;
}

void LauncherWindow::sendFriendRequest(const QString& targetNickname)
{
    QJsonObject body;
    body["p_my_code"] = m_myCode;
    body["p_target_nickname"] = targetNickname;
    QNetworkReply* reply = postFriendsRpc(QStringLiteral("send_friend_request"), body, "send");
    reply->setProperty("target", targetNickname);
}

void LauncherWindow::respondFriendRequest(const QString& requesterNickname, bool accept)
{
    QJsonObject body;
    body["p_my_code"] = m_myCode;
    body["p_requester_nickname"] = requesterNickname;
    body["p_accept"] = accept;
    postFriendsRpc(QStringLiteral("respond_friend_request"), body, "respond");
}

void LauncherWindow::onRespondFriendRequestReply(QNetworkReply* reply)
{
    reply->deleteLater();
    fetchFriends();
}

void LauncherWindow::onWebsiteButtonClicked()
{
    QDesktopServices::openUrl(QUrl(QString::fromUtf8(WEBSITE_URL)));
}

void LauncherWindow::setUpdateStatus(const QString& text, bool error)
{
    setStatus(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color: #e06055;")
                                        : QStringLiteral("color: #0ac8b9;"));
}

void LauncherWindow::onCheckUpdateClicked()
{
    m_updateBtn->setEnabled(false);
    setUpdateStatus(QStringLiteral("Buscando actualizaciones..."));
    checkForUpdates();
}

void LauncherWindow::checkForUpdates()
{
    QNetworkRequest req((QUrl(QString::fromUtf8(UPDATE_MANIFEST_URL))));
    // Sin esto, un manifiesto cacheado haria que nunca se vea una version nueva.
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(8000); // sin red, el arranque no se queda esperando

    QNetworkReply* reply = m_updateNetwork->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            setUpdateStatus(QStringLiteral("No se pudo consultar: %1").arg(reply->errorString()), true);
            finishUpdateFlow();
            return;
        }

        const QJsonObject manifest = QJsonDocument::fromJson(reply->readAll()).object();
        const QString remoteVersion = manifest.value("version").toString();
        if (remoteVersion.isEmpty())
        {
            setUpdateStatus(QStringLiteral("El servidor respondio algo invalido."), true);
            finishUpdateFlow();
            return;
        }

        // Solo se bajan los archivos cuyo hash no coincide con el local: si
        // la ROM no cambio, no se vuelven a bajar 65 MB por gusto.
        m_updateQueue.clear();
        const QDir appDir(QCoreApplication::applicationDirPath());
        for (const QJsonValue& v : manifest.value("files").toArray())
        {
            const QJsonObject o = v.toObject();
            UpdateFile f{o.value("path").toString(), o.value("url").toString(),
                         o.value("sha256").toString().toLower()};
            if (f.path.isEmpty() || f.url.isEmpty())
                continue;

            QFile local(appDir.filePath(f.path));
            if (local.exists() && local.open(QIODevice::ReadOnly))
            {
                QCryptographicHash h(QCryptographicHash::Sha256);
                if (h.addData(&local) && QString::fromLatin1(h.result().toHex()) == f.sha256)
                    continue; // identico, no hace falta
            }
            m_updateQueue.append(f);
        }

        if (m_updateQueue.isEmpty())
        {
            setUpdateStatus(QStringLiteral("Todo al dia (version %1).").arg(remoteVersion));
            finishUpdateFlow();
            return;
        }

        m_updateVersion = remoteVersion;
        m_updateNotes = manifest.value("notes").toString();
        m_updateIndex = 0;
        // Se descarga a una carpeta aparte y solo se reemplaza cuando TODO
        // llego bien: si se corta la conexion a la mitad, la instalacion
        // actual queda intacta en vez de medio actualizada.
        m_updateStageDir = appDir.filePath(QStringLiteral("update-tmp"));
        QDir(m_updateStageDir).removeRecursively();
        QDir().mkpath(m_updateStageDir);

        downloadNextUpdateFile();
    });
}

void LauncherWindow::downloadNextUpdateFile()
{
    if (m_updateIndex >= m_updateQueue.size())
    {
        applyStagedUpdate();
        return;
    }

    const UpdateFile f = m_updateQueue.at(m_updateIndex);
    setUpdateStatus(QStringLiteral("Descargando %1 (%2 de %3)...")
        .arg(f.path).arg(m_updateIndex + 1).arg(m_updateQueue.size()));

    QNetworkRequest req((QUrl(f.url)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy); // GitHub redirige a su CDN
    QNetworkReply* reply = m_updateNetwork->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, f]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            setUpdateStatus(QStringLiteral("Fallo la descarga de %1: %2")
                .arg(f.path, reply->errorString()), true);
            QDir(m_updateStageDir).removeRecursively();
            finishUpdateFlow();
            return;
        }

        const QByteArray data = reply->readAll();
        if (!f.sha256.isEmpty())
        {
            const QString got = QString::fromLatin1(
                QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            if (got != f.sha256)
            {
                setUpdateStatus(QStringLiteral("%1 llego corrupto, se cancela.").arg(f.path), true);
                QDir(m_updateStageDir).removeRecursively();
                finishUpdateFlow();
                return;
            }
        }

        const QString dest = QDir(m_updateStageDir).filePath(f.path);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
        {
            setUpdateStatus(QStringLiteral("No se pudo escribir %1.").arg(f.path), true);
            QDir(m_updateStageDir).removeRecursively();
            finishUpdateFlow();
            return;
        }
        out.close();

        ++m_updateIndex;
        downloadNextUpdateFile();
    });
}

void LauncherWindow::finishUpdateFlow()
{
    m_updateBtn->setEnabled(true);
    if (m_startupUpdateCheck)
    {
        m_startupUpdateCheck = false;
        startAutoConnect();
    }
}

void LauncherWindow::applyStagedUpdate()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString selfPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    bool selfUpdated = false;
    bool failed = false;

    for (const UpdateFile& f : m_updateQueue)
    {
        const QString staged = QDir(m_updateStageDir).filePath(f.path);
        const QString target = appDir.filePath(f.path);
        QDir().mkpath(QFileInfo(target).absolutePath());

        // Windows no deja sobrescribir ni borrar un .exe en ejecucion, pero SI
        // deja renombrarlo: el viejo se corre de lugar (.old) y el nuevo ocupa
        // su nombre. Sirve igual para el launcher que para RMG-K abierto.
        if (QFile::exists(target))
        {
            QFile::remove(target + QStringLiteral(".old"));
            if (!QFile::rename(target, target + QStringLiteral(".old")))
            {
                failed = true;
                continue;
            }
        }
        if (!QFile::rename(staged, target))
        {
            QFile::rename(target + QStringLiteral(".old"), target); // volver atras
            failed = true;
            continue;
        }
        if (QDir::toNativeSeparators(target) == selfPath)
            selfUpdated = true;
    }

    QDir(m_updateStageDir).removeRecursively();

    if (failed)
    {
        setUpdateStatus(QStringLiteral("No se pudo actualizar algun archivo. Cierra el juego y reabri Prince."), true);
        finishUpdateFlow();
        return;
    }

    {
        // Las novedades se muestran despues (al reiniciar, si se actualizo el
        // propio launcher; enseguida si no).
        QSettings s("RMG-K", "n02");
        s.setValue("Launcher/PendingNotesVersion", m_updateVersion);
        s.setValue("Launcher/PendingNotes", m_updateNotes);
    }
    setUpdateStatus(QStringLiteral("Actualizado a la version %1. Reiniciando...").arg(m_updateVersion));

    if (selfUpdated)
    {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {},
                                QCoreApplication::applicationDirPath());
        QTimer::singleShot(400, qApp, &QCoreApplication::quit);
    }
    else
    {
        setUpdateStatus(QStringLiteral("Actualizado a la version %1.").arg(m_updateVersion));
        QTimer::singleShot(400, this, [this]() { showPendingUpdateNotes(); });
        finishUpdateFlow();
    }
}

void LauncherWindow::onCreateShortcutClicked()
{
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktop.isEmpty())
    {
        setStatus(QStringLiteral("No encontre la carpeta del Escritorio."));
        return;
    }

    const QString target = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString workDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString linkPath = QDir(desktop).filePath(QStringLiteral("Prince.lnk"));

    // Un .lnk de verdad se arma con COM (IShellLink). Se hace via PowerShell
    // para no tener que enlazar ole32/shell32 ni manejar COM a mano por un
    // boton; -WindowStyle Hidden para que no parpadee una consola.
    const QString script = QStringLiteral(
        "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%1');"
        "$s.TargetPath='%2';$s.WorkingDirectory='%3';$s.IconLocation='%2,0';$s.Save()")
        .arg(QDir::toNativeSeparators(linkPath), target, workDir);

    const int rc = QProcess::execute(QStringLiteral("powershell"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
         QStringLiteral("-Command"), script});

    if (rc == 0 && QFileInfo::exists(linkPath))
        setStatus(QStringLiteral("Acceso directo creado en el Escritorio."));
    else
        setStatus(QStringLiteral("No se pudo crear el acceso directo."));
}

void LauncherWindow::onPresenceItemDoubleClicked()
{
    QListWidgetItem* item = m_presenceList->currentItem();
    if (item == nullptr)
        return;
    // Doble click = el camino rapido de siempre, clasificatoria por
    // defecto. La amistosa se elige a proposito desde el menu contextual.
    challengePlayer(item->data(Qt::UserRole).toString(), QStringLiteral("ranked"));
}

void LauncherWindow::onPresenceContextMenuRequested(const QPoint& pos)
{
    QListWidgetItem* item = m_presenceList->itemAt(pos);
    if (item == nullptr || m_myCode.isEmpty())
        return;

    const QString nickname = item->data(Qt::UserRole).toString();
    QMenu menu(m_presenceList);
    QAction* rankedAction = menu.addAction(QStringLiteral("Desafiar a %1 (Clasificatoria)").arg(nickname));
    QAction* casualAction = menu.addAction(QStringLiteral("Desafiar a %1 (Amistosa)").arg(nickname));

    // Solo se puede invitar a Team si todavia no estoy en un equipo. Si ya
    // estoy en uno, se muestra apagado para que se entienda por que.
    QAction* teamAction = nullptr;
    if (!m_teamActive)
    {
        teamAction = menu.addAction(QStringLiteral("Invitar a %1 a Team").arg(nickname));
    }
    else
    {
        QAction* inTeam = menu.addAction(QStringLiteral("Invitar a Team (ya estas en un equipo)"));
        inTeam->setEnabled(false);
    }

    QAction* watchAction = isBroadcastingPlayer(nickname)
        ? menu.addAction(QStringLiteral("Ver la partida de %1 en vivo").arg(nickname)) : nullptr;
    QAction* addFriendAction = menu.addAction(QStringLiteral("Agregar a %1 de amigo").arg(nickname));
    QAction* chosen = menu.exec(m_presenceList->mapToGlobal(pos));
    if (chosen == rankedAction)
        challengePlayer(nickname, QStringLiteral("ranked"));
    else if (chosen == casualAction)
        challengePlayer(nickname, QStringLiteral("casual"));
    else if (chosen != nullptr && chosen == teamAction)
    {
        inviteToTeam(nickname);
    }
    else if (chosen != nullptr && chosen == watchAction)
        startWatching(nickname);
    else if (chosen == addFriendAction)
        sendFriendRequest(nickname);
}

void LauncherWindow::challengePlayer(const QString& targetNickname, const QString& matchType)
{
    if (targetNickname.isEmpty() || m_lobbyClient == nullptr || !m_lobbyConnected)
        return;
    sendChallenge(targetNickname, matchType);
}

void LauncherWindow::sendChallenge(const QString& targetNickname, const QString& matchType)
{
    m_pendingChallengeTarget = targetNickname;
    m_pendingChallengeMatchType = matchType;
    setStatus(matchType == QStringLiteral("casual")
        ? QStringLiteral("Enviando desafio amistoso a %1...").arg(targetNickname)
        : QStringLiteral("Enviando desafio clasificatorio a %1...").arg(targetNickname));

    // La clave y el tipo van embebidos en el nombre
    // ("RETO:<rival>:<clave>:<R|C>") y la clave tambien se la pasamos al
    // servidor como password de la sala -- el rival extrae los dos datos del
    // nombre en checkIncomingChallenges() para poder unirse y mostrar el
    // tipo correcto.
    const QString password = generateRoomPassword();
    m_pendingChallengePassword = password;
    const QChar typeChar = (matchType == QStringLiteral("casual")) ? QLatin1Char('C') : QLatin1Char('R');
    m_lobbyClient->createRoom(
        QString(CHALLENGE_ROOM_PREFIX) + targetNickname + QLatin1Char(':') + password + QLatin1Char(':') + typeChar,
        SMASH_REMIX_ROM_NAME,
        m_romMd5,
        QString(), // region: resuelta por el servidor segun el MD5
        2,         // maxPlayers: un desafio siempre es 1v1
        -1,        // delay: Auto
        0,         // prediction: Default
        1,         // pacing: Smooth
        password);
}

void LauncherWindow::inviteToTeam(const QString& targetNickname)
{
    if (targetNickname.isEmpty() || m_lobbyClient == nullptr || !m_lobbyConnected || m_teamActive)
        return;

    m_pendingTeamInviteTarget = targetNickname;
    m_creatingTeamRoom = true;
    setStatus(QStringLiteral("Invitando a %1 a tu equipo...").arg(targetNickname));

    const QString password = generateRoomPassword();
    m_pendingTeamPassword = password;
    m_lobbyClient->createRoom(
        QString(TEAM_ROOM_PREFIX) + targetNickname + QLatin1Char(':') + password,
        SMASH_REMIX_ROM_NAME,
        m_romMd5,
        QString(),
        2,   // maxPlayers: el par (el partido de 4 se arma despues, al retar)
        -1, 0, 1,
        password);
}

void LauncherWindow::onLobbyRoomCreated(quint64 roomId)
{
    if (m_creatingTeamRoom)
    {
        m_creatingTeamRoom = false;
        m_teamRoomId = roomId;
        m_teamRoomPassword = m_pendingTeamPassword;
        const auto it = m_lobbyClient->rooms().constFind(roomId);
        m_teamRoomName = (it != m_lobbyClient->rooms().constEnd()) ? it->name : QString();
        m_teamIsHost = true;
        m_teamActive = true;
        m_teamLocalReady = false;
        setStatus(QStringLiteral("Equipo creado, esperando a %1...").arg(m_pendingTeamInviteTarget));
        refreshTeamDialog();
        return;
    }

    // No nos desconectamos todavia: si lo hicieramos ahora, la sala podria
    // desaparecer antes de que el rival la vea en su lista (bug real que ya
    // encontramos probando esto mismo dentro del juego). Nos quedamos
    // conectados sosteniendola abierta hasta que el rival entre.
    m_outgoingChallenges.append({roomId, m_pendingChallengeTarget, m_pendingChallengePassword, m_pendingChallengeMatchType});
    setStatus(QStringLiteral("Desafio enviado a %1, esperando...").arg(m_pendingChallengeTarget));
    refreshPendingChallengesDisplay();
}

void LauncherWindow::onLobbyRoomCreateFailed(const QString& reason)
{
    if (m_creatingTeamRoom)
    {
        m_creatingTeamRoom = false;
        setStatus(QStringLiteral("No se pudo crear el equipo: %1").arg(reason));
        return;
    }
    setStatus(QStringLiteral("No se pudo enviar el desafio: %1").arg(reason));
}

void LauncherWindow::onLobbyRoomListChanged()
{
    checkIncomingChallenges();
    checkIncomingTeamInvites();

    // Si estoy en una sala de Team, cada cambio en la lista puede ser
    // alguien mas sentandose -- refrescar cuenta/miembros y ver si ya
    // completamos 4 y todos dijeron Listo.
    if (m_teamActive)
        refreshTeamDialog();

    for (int i = 0; i < m_outgoingChallenges.size(); ++i)
    {
        const PendingChallenge pending = m_outgoingChallenges.at(i);
        const auto it = m_lobbyClient->rooms().constFind(pending.roomId);
        if (it != m_lobbyClient->rooms().constEnd() && it->players >= 2)
        {
            m_outgoingChallenges.removeAt(i);
            refreshPendingChallengesDisplay();
            setStatus(QStringLiteral("%1 acepto! Preparando la sala...").arg(pending.nickname));
            enterPreGameRoom(pending.roomId, pending.nickname, /*isHost=*/true, pending.password, pending.matchType);
            return;
        }
    }
}

void LauncherWindow::checkIncomingChallenges()
{
    if (m_lobbyClient == nullptr || m_myNickname.isEmpty())
        return;

    const QString myRoomPrefix = QString(CHALLENGE_ROOM_PREFIX) + m_myNickname + QLatin1Char(':');
    QList<PendingChallenge> found;
    for (auto it = m_lobbyClient->rooms().constBegin(); it != m_lobbyClient->rooms().constEnd(); ++it)
    {
        if (it->name.startsWith(myRoomPrefix) && it->state == QStringLiteral("waiting"))
        {
            // Resto = "<password>:<R|C>" -- el password nunca lleva ':', asi
            // que separar por el ultimo ':' es seguro.
            const QString rest = it->name.mid(myRoomPrefix.length());
            const int sep = rest.lastIndexOf(QLatin1Char(':'));
            const QString password = (sep >= 0) ? rest.left(sep) : rest;
            const QString typeChar = (sep >= 0) ? rest.mid(sep + 1) : QString();
            const QString matchType = (typeChar == QStringLiteral("C"))
                ? QStringLiteral("casual") : QStringLiteral("ranked");
            found.append({it->id, it->hostName, password, matchType});
        }
    }

    bool changed = found.size() != m_incomingChallenges.size();
    QList<PendingChallenge> newlyArrived;
    for (const PendingChallenge& f : found)
    {
        bool stillThere = false;
        for (const PendingChallenge& existing : m_incomingChallenges)
        {
            if (existing.roomId == f.roomId) { stillThere = true; break; }
        }
        if (!stillThere)
        {
            changed = true;
            newlyArrived.append(f);
        }
    }

    if (changed)
    {
        m_incomingChallenges = found;
        refreshPendingChallengesDisplay();
        for (const PendingChallenge& f : newlyArrived)
        {
            // Si estoy buscando partida y me reta alguien que tambien busca,
            // es el emparejamiento automatico: se acepta solo.
            if (m_searching && f.matchType == QStringLiteral("ranked") && m_searchers.contains(f.nickname))
            {
                const quint64 roomId = f.roomId;
                setSearching(false);
                respondToChallenge(roomId, true);
                continue;
            }
            fetchHeadToHead(f.nickname);
            notifyIncomingChallenge(f.nickname);
        }
    }
}

void LauncherWindow::checkIncomingTeamInvites()
{
    if (m_lobbyClient == nullptr || m_myNickname.isEmpty() || m_teamActive)
        return; // ya estoy en un equipo, no busco invitaciones nuevas (la
                 // primera invitacion es por sala; una vez adentro las demas
                 // llegan por chat, ver onLobbyChatMessageReceived)

    const QString myPrefix = QString(TEAM_ROOM_PREFIX) + m_myNickname + QLatin1Char(':');
    QList<PendingTeamInvite> found;
    for (auto it = m_lobbyClient->rooms().constBegin(); it != m_lobbyClient->rooms().constEnd(); ++it)
    {
        if (it->name.startsWith(myPrefix) && it->state == QStringLiteral("waiting"))
            found.append({it->id, it->hostName, it->name.mid(myPrefix.length())});
    }

    bool changed = found.size() != m_incomingTeamInvites.size();
    QList<PendingTeamInvite> newlyArrived;
    for (const PendingTeamInvite& f : found)
    {
        bool stillThere = false;
        for (const PendingTeamInvite& existing : m_incomingTeamInvites)
            if (existing.roomId == f.roomId) { stillThere = true; break; }
        if (!stillThere) { changed = true; newlyArrived.append(f); }
    }

    if (changed)
    {
        m_incomingTeamInvites = found;
        refreshPendingChallengesDisplay();
        for (const PendingTeamInvite& f : newlyArrived)
            notifyIncomingChallenge(f.fromNickname);
    }
}

void LauncherWindow::refreshPendingChallengesDisplay()
{
    clearLayout(m_pendingLayout);

    // Los que me llegaron van primero (necesitan mi accion), en dorado y
    // pulsando para que no pasen desapercibidos.
    for (const PendingChallenge& challenge : m_incomingChallenges)
    {
        const quint64 roomId = challenge.roomId;
        QString label = (challenge.matchType == QStringLiteral("casual"))
            ? QStringLiteral("%1 te envio un desafio amistoso!").arg(challenge.nickname)
            : QStringLiteral("%1 te envio un desafio clasificatorio!").arg(challenge.nickname);
        if (m_h2hCache.contains(challenge.nickname))
            label += QStringLiteral("  (%1)").arg(m_h2hCache.value(challenge.nickname));
        addActionRow(m_pendingLayout, label, "#f5c542",
                     [this, roomId]() { respondToChallenge(roomId, true); },
                     [this, roomId]() { respondToChallenge(roomId, false); },
                     /*pulse=*/true);
    }

    for (const PendingTeamInvite& invite : m_incomingTeamInvites)
    {
        const quint64 roomId = invite.roomId;
        const QString password = invite.password;
        const QString from = invite.fromNickname;
        addActionRow(m_pendingLayout, QStringLiteral("%1 te invito a su equipo!").arg(from), "#f5c542",
                     [this, roomId, password, from]() { respondToTeamInvite(roomId, true, password, from); },
                     [this, roomId, password, from]() { respondToTeamInvite(roomId, false, password, from); },
                     /*pulse=*/true);
    }

    for (const TeamChallenge& tc : m_incomingTeamChallenges)
    {
        const QString from = tc.fromHost;
        addActionRow(m_pendingLayout,
                     QStringLiteral("Equipo %1 + %2 te reto a un 2v2!").arg(tc.fromHost, tc.fromPartner),
                     "#f5c542",
                     [this, from]() { respondToTeamChallenge(from, true); },
                     [this, from]() { respondToTeamChallenge(from, false); },
                     /*pulse=*/true);
    }

    for (const PendingChallenge& challenge : m_outgoingChallenges)
    {
        addPlainRow(m_pendingLayout, QStringLiteral("Esperando respuesta de %1...").arg(challenge.nickname),
                    QColor(0x78, 0x5a, 0x28));
    }

    const int total = m_incomingChallenges.size() + m_incomingTeamInvites.size()
        + m_incomingTeamChallenges.size() + m_outgoingChallenges.size();
    m_pendingSection->setVisible(total > 0);
    if (total > 0)
        fitScrollHeight(m_pendingScroll, total);
}

void LauncherWindow::respondToTeamInvite(quint64 roomId, bool accept, const QString& password,
                                          const QString& fromNickname)
{
    for (int i = 0; i < m_incomingTeamInvites.size(); ++i)
    {
        if (m_incomingTeamInvites.at(i).roomId == roomId)
        {
            m_incomingTeamInvites.removeAt(i);
            break;
        }
    }
    refreshPendingChallengesDisplay();

    if (!accept || m_teamActive || m_lobbyClient == nullptr)
        return;

    m_teamRoomId = roomId;
    m_teamRoomPassword = password;
    const auto it = m_lobbyClient->rooms().constFind(roomId);
    m_teamRoomName = (it != m_lobbyClient->rooms().constEnd()) ? it->name : QString();
    m_teamIsHost = false;
    m_teamActive = true;
    m_teamLocalReady = false;
    setStatus(QStringLiteral("Uniendote al equipo de %1...").arg(fromNickname));
    m_lobbyClient->joinRoom(roomId, password);
}

void LauncherWindow::respondToChallenge(quint64 roomId, bool accept)
{
    QString password;
    QString opponentNickname;
    QString matchType = QStringLiteral("ranked");
    for (int i = 0; i < m_incomingChallenges.size(); ++i)
    {
        if (m_incomingChallenges.at(i).roomId == roomId)
        {
            password = m_incomingChallenges.at(i).password;
            opponentNickname = m_incomingChallenges.at(i).nickname;
            matchType = m_incomingChallenges.at(i).matchType;
            m_incomingChallenges.removeAt(i);
            break;
        }
    }
    refreshPendingChallengesDisplay();

    if (!accept)
        return;

    // onLobbyRoomJoinOk no trae el nombre del rival ni el tipo -- se guardan
    // aca para poder abrir la sala previa con los datos correctos.
    m_pendingJoinOpponent = opponentNickname;
    m_pendingJoinPassword = password;
    m_pendingJoinMatchType = matchType;
    setStatus(QStringLiteral("Uniendose a la sala..."));
    m_lobbyClient->joinRoom(roomId, password);
}

void LauncherWindow::onLobbyRoomJoinOk(quint64 roomId)
{
    if (m_teamActive && m_teamRoomId == roomId)
    {
        setStatus(QStringLiteral("Unido al equipo."));
        refreshTeamDialog();
        return;
    }
    setStatus(QStringLiteral("Conectado a la sala."));
    enterPreGameRoom(roomId, m_pendingJoinOpponent, /*isHost=*/false, m_pendingJoinPassword, m_pendingJoinMatchType);
}

void LauncherWindow::onLobbyRoomJoinFailed(const QString& reason)
{
    if (m_teamActive && m_teamRoomId != 0)
    {
        // El join fallo antes de confirmarse -- no quedo sentado en ningun
        // lado, asi que se limpia el estado de equipo entero.
        m_teamActive = false;
        m_teamRoomId = 0;
    }
    setStatus(QStringLiteral("No se pudo unir a la partida: %1").arg(reason));
}

void LauncherWindow::logOut()
{
    if (m_gameProcessId != 0)
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(m_gameProcessId));
        DWORD code = 0;
        const bool alive = (h != nullptr && GetExitCodeProcess(h, &code) && code == STILL_ACTIVE);
        if (h != nullptr)
            CloseHandle(h);
        if (alive)
        {
            QMessageBox::information(this, QStringLiteral("Cerrar sesion"),
                QStringLiteral("Cierra la partida abierta antes de cerrar sesion."));
            return;
        }
    }

    if (QMessageBox::question(this, QStringLiteral("Cerrar sesion"),
            QStringLiteral("Cerrar la sesion de %1?\n\nLa proxima vez tendras que escribir tu codigo.")
                .arg(m_myNickname)) != QMessageBox::Yes)
        return;

    if (m_preGameDialog)
        leavePreGameRoom();
    if (m_teamActive)
        leaveTeamRoom();
    if (m_presenceTimer != nullptr)
        m_presenceTimer->stop();
    if (m_friendsTimer != nullptr)
        m_friendsTimer->stop();
    setSearching(false);
    setPresenceMode(QStringLiteral("online"));
    if (m_rematchBtn != nullptr)
        m_rematchBtn->hide();
    m_h2hCache.clear();
    for (auto it = m_dmDialogs.begin(); it != m_dmDialogs.end(); ++it)
        if (!it.value().isNull())
            it.value()->close();
    m_dmDialogs.clear();
    m_dmLogs.clear();
    m_dmLastId.clear();
    m_dmUnread.clear();
    m_dmNotifiedId.clear();
    m_knownIncomingRequests.clear();
    m_knownOutgoing.clear();
    m_friendsLoadedOnce = false;
    m_achChecked = false;
    m_myProfileId.clear();
    showListTab(false);
    if (m_lobbyClient != nullptr)
        m_lobbyClient->disconnectFromServer();

    QSettings("RMG-K", "n02").remove("Launcher/PlayerCode");
    m_myCode.clear();
    m_myNickname.clear();
    m_lobbyConnected = false;
    m_connectAttempts = 0;
    m_pendingWatchNick.clear();
    m_friends.clear();
    m_incomingChallenges.clear();
    m_outgoingChallenges.clear();
    m_incomingTeamInvites.clear();
    m_chatLog->clear();

    refreshRosterDisplay();
    refreshFriendsDisplay();
    refreshPendingChallengesDisplay();

    m_accountCard->hide();
    m_codeInput->clear();
    m_codeInput->setEnabled(true);
    m_connectBtn->setEnabled(true);
    m_accountBox->show();
    m_preLoginStatus->setText(QStringLiteral("Sesion cerrada. Escribi tu codigo y confirma con Conectar."));
    m_preLoginStatus->show();
    m_chatInput->setEnabled(false);
    m_chatSendBtn->setEnabled(false);
}

QWidget* LauncherWindow::buildRecordOptions(QWidget* parent, bool isHost)
{
    QSettings settings("RMG-K", "n02");
    m_optRecord = settings.value("Launcher/RecordGame", false).toBool();
    m_optLiveReplay = isHost && settings.value("Launcher/LiveReplay", false).toBool();
    if (m_optLiveReplay)
        m_optRecord = true;

    auto* box = new QWidget(parent);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    auto* rec = new QCheckBox(QStringLiteral("Grabar partida (archivo .krec en esta PC)"), box);
    rec->setToolTip(QStringLiteral("Guarda una repeticion de la partida en tu PC. Es solo tuya: cada jugador decide."));
    rec->setChecked(m_optRecord);
    lay->addWidget(rec);

    QCheckBox* live = nullptr;
    if (isHost)
    {
        live = new QCheckBox(QStringLiteral("Live Replay (otros pueden verla desde la pagina)"), box);
        live->setToolTip(QStringLiteral("Transmite la partida para que cualquiera con Prince la vea en vivo. "
                                        "Incluye grabarla."));
        live->setChecked(m_optLiveReplay);
        lay->addWidget(live);
    }

    connect(rec, &QCheckBox::toggled, this, [this, rec, live](bool on) {
        if (!on && live != nullptr && live->isChecked())
        {
            rec->setChecked(true); // Live Replay necesita la grabacion
            return;
        }
        m_optRecord = on;
        QSettings("RMG-K", "n02").setValue("Launcher/RecordGame", on);
    });
    if (live != nullptr)
    {
        connect(live, &QCheckBox::toggled, this, [this, rec](bool on) {
            m_optLiveReplay = on;
            if (on)
                rec->setChecked(true);
            QSettings("RMG-K", "n02").setValue("Launcher/LiveReplay", on);
        });
    }
    return box;
}

void LauncherWindow::handleWatchUrl(const QString& url)
{
    const QUrl parsed(url);
    if (parsed.scheme().compare(QStringLiteral("prince"), Qt::CaseInsensitive) != 0 ||
        parsed.host().compare(QStringLiteral("watch"), Qt::CaseInsensitive) != 0)
        return;
    const QString nick = QUrlQuery(parsed).queryItemValue(QStringLiteral("nick"), QUrl::FullyDecoded);
    if (nick.isEmpty())
        return;

    showNormal();
    raise();
    activateWindow();

    if (m_myNickname.isEmpty())
    {
        // Todavia no se resolvio la cuenta: se retoma apenas conecte.
        m_pendingWatchNick = nick;
        setStatus(QStringLiteral("Conectando para ver la partida de %1...").arg(nick));
        return;
    }
    startWatching(nick);
}

void LauncherWindow::startWatching(const QString& targetNickname)
{
    if (m_gameProcessId != 0)
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(m_gameProcessId));
        DWORD code = 0;
        const bool alive = (h != nullptr && GetExitCodeProcess(h, &code) && code == STILL_ACTIVE);
        if (h != nullptr)
            CloseHandle(h);
        if (alive)
        {
            setStatus(QStringLiteral("Ya hay una partida abierta: cierrala para ver otra."));
            return;
        }
    }
    if (m_romMd5.isEmpty())
    {
        setStatus(QStringLiteral("No encontre la ROM junto al launcher, no se puede ver la partida."));
        return;
    }

    const QString exeDir = QCoreApplication::applicationDirPath();
    QStringList args;
    args << QStringLiteral("--lobby-nickname=%1").arg(m_myNickname)
         << QStringLiteral("--lobby-spectate=%1").arg(targetNickname)
         << QStringLiteral("--lobby-match-rom=%1").arg(romFilePath())
         << QStringLiteral("--lobby-match-rom-md5=%1").arg(m_romMd5);

    // Igual que en una partida: el launcher suelta su conexion al Lobby
    // mientras RMG-K usa la suya, y la retoma cuando RMG-K se cierra.
    if (m_lobbyClient != nullptr)
        m_lobbyClient->disconnectFromServer();

    m_gameProcessId = 0;
    m_gameOpponent.clear();
    QProcess::startDetached(exeDir + QStringLiteral("/RMG-K.exe"), args, exeDir, &m_gameProcessId);
    reportPresence();
    setStatus(QStringLiteral("Abriendo la partida de %1...").arg(targetNickname));
    watchGameProcess();
}

QString LauncherWindow::romFilePath() const
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QString::fromLatin1(SMASH_REMIX_ROM_FILE));
}

void LauncherWindow::handOffToGame()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString rmgkPath = exeDir + QStringLiteral("/RMG-K.exe");

    // Se copian antes de limpiar el estado de la sala previa.
    const QString roomName = m_activeRoomName;
    const QString password = m_activeRoomPassword;
    const QString opponent = m_activeOpponent;
    const QString matchType = m_activeMatchType;
    const bool    isHost   = m_activeIsHost;

    QStringList args;
    args << QStringLiteral("--lobby-nickname=%1").arg(m_myNickname)
         << QStringLiteral("--lobby-match-room=%1").arg(roomName)
         << QStringLiteral("--lobby-match-password=%1").arg(password)
         // Sin el rival, live_reader.py no puede armar la identidad de la
         // partida y la pagina no recibe el resultado.
         << QStringLiteral("--lobby-match-opponent=%1").arg(opponent)
         << QStringLiteral("--lobby-match-type=%1").arg(matchType)
         << QStringLiteral("--lobby-match-rom=%1").arg(romFilePath())
         << QStringLiteral("--lobby-match-rom-md5=%1").arg(m_romMd5);
    if (isHost)
        args << QStringLiteral("--lobby-match-host");
    if (m_optRecord || (isHost && m_optLiveReplay))
        args << QStringLiteral("--lobby-record");
    if (isHost && m_optLiveReplay)
        args << QStringLiteral("--lobby-live-replay");

    leavePreGameRoom();

    // Soltar la sala del launcher ANTES de arrancar RMG-K: el que retó la va
    // a recrear con el mismo nombre desde RMG-K, y si la vieja siguiera viva
    // el nombre estaria duplicado y el rival podria engancharse a la que ya
    // no sirve.
    if (m_lobbyClient != nullptr)
        m_lobbyClient->disconnectFromServer();

    m_gameProcessId = 0;
    m_gameOpponent = opponent;
    m_lastMatchType = matchType;
    if (m_rematchBtn != nullptr)
        m_rematchBtn->hide();
    QProcess::startDetached(rmgkPath, args, exeDir, &m_gameProcessId);
    reportPresence(); // la web pasa a "jugando contra X" de inmediato

    setStatus(isHost
        ? QStringLiteral("Abriendo la partida, esperando a %1...").arg(opponent)
        : QStringLiteral("Abriendo la partida..."));

    watchGameProcess();
}

void LauncherWindow::watchGameProcess()
{
    // Al cerrar el juego hay que volver al Lobby solo, si no el launcher
    // queda mudo (sin jugadores, sin chat) hasta que lo reabras a mano.
    // Compartido entre 1v1 y Team: antes esto solo se creaba dentro de
    // handOffToGame(), asi que si la PRIMERA partida de la sesion era de
    // Team, el timer nunca se creaba y el launcher no se enteraba de que
    // RMG-K se habia cerrado.
    if (m_gameProcessId == 0)
        return;

    if (m_gameWatchTimer == nullptr)
    {
        m_gameWatchTimer = new QTimer(this);
        m_gameWatchTimer->setInterval(3000);
        connect(m_gameWatchTimer, &QTimer::timeout, this, [this]() {
            if (m_gameProcessId == 0)
            {
                m_gameWatchTimer->stop();
                return;
            }
            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                      static_cast<DWORD>(m_gameProcessId));
            bool running = false;
            if (proc != nullptr)
            {
                DWORD exitCode = 0;
                running = GetExitCodeProcess(proc, &exitCode) && exitCode == STILL_ACTIVE;
                CloseHandle(proc);
            }
            if (!running)
            {
                const QString finishedOpponent = m_gameOpponent;
                m_gameProcessId = 0;
                m_gameOpponent.clear();
                m_gameWatchTimer->stop();
                reportPresence(); // dejo de estar "en partida"
                reconnectToLobby();
                offerRematch(finishedOpponent, m_lastMatchType);
                // La partida recien terminada ya cuenta: se refresca el nivel.
                QTimer::singleShot(6000, this, [this]() { fetchLevels(); fetchFullRoster(); });
            }
        });
    }
    m_gameWatchTimer->start();
}

void LauncherWindow::reportPresence()
{
    if (m_myCode.isEmpty())
        return;

    const bool playing = (m_gameProcessId != 0);

    QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/rpc/report_presence").arg(SUPABASE_URL)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);

    QJsonObject body;
    body["p_my_code"] = m_myCode;
    body["p_status"] = playing ? QStringLiteral("playing") : m_presenceMode;
    body["p_opponent"] = playing ? m_gameOpponent : QString();
    m_presenceNetwork->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

bool LauncherWindow::eventFilter(QObject* watched, QEvent* event)
{
    QListWidget* list = nullptr;
    if (m_presenceList != nullptr && watched == m_presenceList->viewport())
        list = m_presenceList;
    else if (m_friendsList != nullptr && watched == m_friendsList->viewport())
        list = m_friendsList;

    if (list != nullptr && event->type() == QEvent::Wheel)
    {
        auto* wheel = static_cast<QWheelEvent*>(event);
        QScrollBar* bar = list->verticalScrollBar();
        QPropertyAnimation*& anim = (list == m_presenceList) ? m_scrollAnim : m_friendsScrollAnim;

        if (anim == nullptr)
        {
            anim = new QPropertyAnimation(bar, "value", this);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->setDuration(260);
        }

        // Si ya hay una animacion en curso se parte desde su destino y no
        // desde donde esta ahora: si no, girar la rueda varias veces seguidas
        // reinicia el recorrido y el scroll se siente trabado.
        const int from = bar->value();
        const int base = (anim->state() == QAbstractAnimation::Running)
            ? anim->endValue().toInt() : from;

        const int steps = wheel->angleDelta().y() / 120;
        const int target = qBound(bar->minimum(), base - steps * 58, bar->maximum());

        anim->stop();
        anim->setStartValue(from);
        anim->setEndValue(target);
        anim->start();
        return true; // consumido: el salto por defecto no debe ocurrir
    }
    return QWidget::eventFilter(watched, event);
}

void LauncherWindow::closeEvent(QCloseEvent* event)
{
    // Cerrar el launcher es la unica desconexion que se puede saber con
    // certeza; el resto (corte de luz, cierre forzado) lo resuelve la web
    // por antiguedad del reporte.
    if (!m_myCode.isEmpty() && m_presenceNetwork != nullptr)
    {
        QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/rpc/clear_presence").arg(SUPABASE_URL)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("apikey", SUPABASE_ANON_KEY);
        req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);

        QJsonObject body;
        body["p_my_code"] = m_myCode;
        QNetworkReply* reply = m_presenceNetwork->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        // Darle un instante para que salga antes de que muera el proceso.
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(800, &loop, &QEventLoop::quit);
        loop.exec();
    }
    QWidget::closeEvent(event);
}

void LauncherWindow::refreshTeamDialog()
{
    if (!m_teamActive)
    {
        if (m_teamDialog)
            m_teamDialog->close();
        return;
    }

    QStringList members;
    int playerCount = 0;
    if (m_lobbyClient != nullptr)
    {
        const auto it = m_lobbyClient->rooms().constFind(m_teamRoomId);
        if (it != m_lobbyClient->rooms().constEnd())
        {
            members = it->playerNames;
            playerCount = it->players;
        }
    }
    m_teamMemberNicknames = members;

    QString partner;
    for (const QString& n : members)
        if (n != m_myNickname)
            partner = n;
    const bool duoFull = (playerCount >= 2 && !partner.isEmpty());

    // Si el companero se fue, el equipo deja de estar armado.
    if (m_duoArmed && !duoFull)
        disarmDuo();

    if (!m_teamDialog)
    {
        auto* dialog = new QDialog(this);
        dialog->setWindowTitle(QStringLiteral("Sala de Team"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(430, 500);

        auto* layout = new QVBoxLayout(dialog);

        auto* title = new QLabel(QStringLiteral("Equipo de 2  ·  2v2"), dialog);
        title->setStyleSheet(QStringLiteral("font-weight:700; font-size:15px;"));
        layout->addWidget(title);

        m_teamMembersLabel = new QLabel(dialog);
        m_teamMembersLabel->setWordWrap(true);
        layout->addWidget(m_teamMembersLabel);

        m_teamStatusLabel = new QLabel(dialog);
        m_teamStatusLabel->setWordWrap(true);
        layout->addWidget(m_teamStatusLabel);

        layout->addWidget(buildRecordOptions(dialog, m_teamIsHost));

        m_teamReadyBtn = new QPushButton(QStringLiteral("Listo"), dialog);
        m_teamReadyBtn->setObjectName("primaryBtn");
        layout->addWidget(m_teamReadyBtn);
        connect(m_teamReadyBtn, &QPushButton::clicked, this, [this]() {
            // Solo el companero da Listo; el anfitrion ya esta implicito.
            if (m_teamLocalReady || m_teamIsHost)
                return;
            m_teamLocalReady = true;
            if (m_teamReadyBtn != nullptr)
            {
                m_teamReadyBtn->setEnabled(false);
                m_teamReadyBtn->setText(QStringLiteral("Listo"));
            }
            if (m_lobbyClient != nullptr)
            {
                // Canal "room": llega solo a los dos sentados en la sala.
                m_lobbyClient->sendChat(QStringLiteral("room"),
                    QStringLiteral("__DUO_READY__|%1|%2").arg(m_teamRoomId).arg(m_myNickname));
            }
            m_duoArmed = true; // no depende de que el servidor me devuelva mi propio mensaje
            refreshTeamDialog();
        });

        // Lista de equipos armados: solo la ve el anfitrion, cuando su
        // equipo ya esta armado.
        m_teamChallengeBox = new QWidget(dialog);
        auto* boxLay = new QVBoxLayout(m_teamChallengeBox);
        boxLay->setContentsMargins(0, 0, 0, 0);
        auto* listLabel = new QLabel(QStringLiteral("Equipos listos para retar:"), m_teamChallengeBox);
        listLabel->setStyleSheet(QStringLiteral("font-weight:700;"));
        boxLay->addWidget(listLabel);
        m_teamDuoList = new QListWidget(m_teamChallengeBox);
        m_teamDuoList->setMinimumHeight(120);
        boxLay->addWidget(m_teamDuoList, 1);
        m_teamChallengeBtn = new QPushButton(QStringLiteral("Retar al equipo seleccionado"), m_teamChallengeBox);
        m_teamChallengeBtn->setObjectName("primaryBtn");
        m_teamChallengeBtn->setEnabled(false);
        boxLay->addWidget(m_teamChallengeBtn);
        layout->addWidget(m_teamChallengeBox, 1);
        connect(m_teamDuoList, &QListWidget::itemSelectionChanged, this, [this]() {
            if (m_teamChallengeBtn != nullptr && m_teamDuoList != nullptr)
                m_teamChallengeBtn->setEnabled(m_teamDuoList->currentItem() != nullptr
                                               && m_duoArmed && m_teamChallengeTarget.isEmpty());
        });
        connect(m_teamChallengeBtn, &QPushButton::clicked, this, [this]() {
            QListWidgetItem* item = m_teamDuoList != nullptr ? m_teamDuoList->currentItem() : nullptr;
            if (item == nullptr)
                return;
            challengeDuo(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toString());
        });

        auto* leaveBtn = new QPushButton(QStringLiteral("Salir del equipo"), dialog);
        layout->addWidget(leaveBtn);
        connect(leaveBtn, &QPushButton::clicked, this, [this]() { leaveTeamRoom(); });

        m_teamDialog = dialog;
        dialog->show();

        if (m_teamTimer == nullptr)
        {
            m_teamTimer = new QTimer(this);
            m_teamTimer->setInterval(4000);
            connect(m_teamTimer, &QTimer::timeout, this, &LauncherWindow::teamTick);
        }
        m_teamTickCount = 0;
        m_teamTimer->start();
    }

    if (m_teamMembersLabel != nullptr)
    {
        m_teamMembersLabel->setText(QStringLiteral("Jugadores (%1/2): %2")
            .arg(playerCount).arg(members.isEmpty() ? QStringLiteral("-") : members.join(QStringLiteral(", "))));
    }

    if (m_teamStatusLabel != nullptr)
    {
        QString text;
        if (m_teamIsHost)
        {
            if (!duoFull)
                text = QStringLiteral("Esperando a que %1 acepte...").arg(m_pendingTeamInviteTarget);
            else if (!m_duoArmed)
                text = QStringLiteral("%1 ya esta en tu equipo. Esperando a que de Listo.").arg(partner);
            else if (m_teamChallengeTarget.isEmpty())
                text = QStringLiteral("Equipo listo! Elegi un equipo de la lista y retalo.");
            else
                text = QStringLiteral("Reto enviado a %1, esperando respuesta...").arg(m_teamChallengeTarget);
        }
        else
        {
            if (!duoFull)
                text = QStringLiteral("Uniendote al equipo...");
            else if (!m_teamLocalReady)
                text = QStringLiteral("Dale a Listo para armar el equipo con %1.").arg(partner);
            else if (!m_duoArmed)
                text = QStringLiteral("Esperando...");
            else
                text = QStringLiteral("Equipo listo! Esperando a que %1 rete a otro equipo, o a que los reten.").arg(partner);
        }
        m_teamStatusLabel->setText(text);
    }

    if (m_teamReadyBtn != nullptr)
    {
        if (m_teamIsHost)
        {
            m_teamReadyBtn->setEnabled(false);
            m_teamReadyBtn->setText(m_duoArmed ? QStringLiteral("Equipo listo")
                                               : QStringLiteral("Esperando a tu companero..."));
        }
        else if (!m_teamLocalReady)
        {
            m_teamReadyBtn->setEnabled(duoFull);
            m_teamReadyBtn->setText(QStringLiteral("Listo"));
        }
    }

    if (m_teamChallengeBox != nullptr)
        m_teamChallengeBox->setVisible(m_teamIsHost && m_duoArmed);
    refreshDuoList();
}

void LauncherWindow::refreshDuoList()
{
    if (m_teamDuoList == nullptr)
        return;

    const QString selectedHost = m_teamDuoList->currentItem() != nullptr
        ? m_teamDuoList->currentItem()->data(Qt::UserRole).toString() : QString();

    m_teamDuoList->clear();
    for (const AvailableDuo& duo : m_availableDuos)
    {
        if (duo.roomId == m_teamRoomId)
            continue; // mi propio equipo
        auto* item = new QListWidgetItem(QStringLiteral("%1  +  %2").arg(duo.host, duo.partner), m_teamDuoList);
        item->setData(Qt::UserRole, duo.host);
        item->setData(Qt::UserRole + 1, duo.partner);
        if (duo.host == selectedHost)
            m_teamDuoList->setCurrentItem(item);
    }
    if (m_teamDuoList->count() == 0)
    {
        auto* empty = new QListWidgetItem(
            QStringLiteral("Todavia no hay otros equipos listos. Aparecen aca cuando alguno se arme."),
            m_teamDuoList);
        empty->setFlags(Qt::NoItemFlags);
    }
    if (m_teamChallengeBtn != nullptr)
        m_teamChallengeBtn->setEnabled(m_teamDuoList->currentItem() != nullptr
                                       && m_duoArmed && m_teamChallengeTarget.isEmpty());
}

void LauncherWindow::disarmDuo()
{
    if (!m_duoArmed)
        return;
    m_duoArmed = false;
    m_teamLocalReady = false;
    m_teamChallengeTarget.clear();
    m_incomingTeamChallenges.clear();
    refreshPendingChallengesDisplay();
    if (m_teamIsHost)
        broadcastDuoOff();
}

void LauncherWindow::broadcastDuo()
{
    if (!m_teamActive || !m_teamIsHost || !m_duoArmed || !m_lobbyConnected || m_lobbyClient == nullptr)
        return;
    QString partner;
    for (const QString& n : m_teamMemberNicknames)
        if (n != m_myNickname)
            partner = n;
    if (partner.isEmpty())
        return;
    // Mensaje tecnico por el canal "lobby": nunca se muestra en el chat.
    m_lobbyClient->sendChat(QStringLiteral("lobby"),
        QStringLiteral("__DUO__|%1|%2|%3").arg(m_teamRoomId).arg(m_myNickname, partner));
}

void LauncherWindow::broadcastDuoOff()
{
    if (m_lobbyClient == nullptr || !m_lobbyConnected || m_teamRoomId == 0)
        return;
    m_lobbyClient->sendChat(QStringLiteral("lobby"), QStringLiteral("__DUO_OFF__|%1").arg(m_teamRoomId));
}

void LauncherWindow::teamTick()
{
    if (!m_teamActive)
    {
        if (m_teamTimer != nullptr)
            m_teamTimer->stop();
        return;
    }

    // Un equipo que dejo de anunciarse (cerro el launcher, se cayo) sale de
    // la lista solo.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (int i = m_availableDuos.size() - 1; i >= 0; --i)
    {
        if (now - m_availableDuos.at(i).lastSeenMs > 22000)
        {
            m_availableDuos.removeAt(i);
            changed = true;
        }
    }
    if (changed)
        refreshDuoList();

    // Aviso cada ~8 s (cada 2 ticks) mientras mi equipo esta armado.
    if ((m_teamTickCount++ % 2) == 0)
        broadcastDuo();
}

void LauncherWindow::challengeDuo(const QString& targetHost, const QString& targetPartner)
{
    if (!m_teamActive || !m_teamIsHost || !m_duoArmed || m_lobbyClient == nullptr || targetHost.isEmpty())
        return;
    QString partner;
    for (const QString& n : m_teamMemberNicknames)
        if (n != m_myNickname)
            partner = n;
    if (partner.isEmpty())
        return;

    m_teamChallengeTarget = targetHost;
    m_lobbyClient->sendChat(QStringLiteral("lobby"),
        QStringLiteral("__TEAM_CHAL__|%1|%2|%3|%4|%5")
            .arg(m_teamRoomId).arg(m_myNickname, partner, targetHost, targetPartner));
    setStatus(QStringLiteral("Reto enviado al equipo de %1.").arg(targetHost));
    refreshTeamDialog();
}

void LauncherWindow::respondToTeamChallenge(const QString& fromHost, bool accept)
{
    QString fromPartner;
    for (int i = 0; i < m_incomingTeamChallenges.size(); ++i)
    {
        if (m_incomingTeamChallenges.at(i).fromHost == fromHost)
        {
            fromPartner = m_incomingTeamChallenges.at(i).fromPartner;
            m_incomingTeamChallenges.removeAt(i);
            break;
        }
    }
    refreshPendingChallengesDisplay();

    if (m_lobbyClient == nullptr || fromPartner.isEmpty())
        return;

    if (!accept)
    {
        m_lobbyClient->sendChat(QStringLiteral("lobby"),
            QStringLiteral("__TEAM_CHAL_NO__|%1|%2").arg(fromHost, m_myNickname));
        return;
    }

    if (!m_teamActive || !m_teamIsHost || !m_duoArmed)
        return;
    QString myPartner;
    for (const QString& n : m_teamMemberNicknames)
        if (n != m_myNickname)
            myPartner = n;
    if (myPartner.isEmpty())
        return;

    // Asientos: P1+P2 = el equipo que reto, P3+P4 = el retado (yo y mi
    // companero). El que reto crea la sala de RMG-K; los demas la buscan.
    const QStringList members{fromHost, fromPartner, m_myNickname, myPartner};
    const QString roomName = QStringLiteral("TM-") + generateRoomPassword();
    const QString password = generateRoomPassword();

    m_lobbyClient->sendChat(QStringLiteral("lobby"),
        QStringLiteral("__TEAM_GO__|%1|%2|%3|%4|%5|%6")
            .arg(roomName, password, members.at(0), members.at(1), members.at(2), members.at(3)));

    handOffTeamMatch(roomName, password, members, /*isMatchHost=*/false);
}

void LauncherWindow::leaveTeamRoom()
{
    if (m_teamActive && m_teamIsHost && m_duoArmed)
        broadcastDuoOff();
    if (m_lobbyClient != nullptr && m_teamActive)
        m_lobbyClient->leaveRoom();

    m_teamActive = false;
    m_teamIsHost = false;
    m_teamRoomId = 0;
    m_teamRoomName.clear();
    m_teamRoomPassword.clear();
    m_teamMemberNicknames.clear();
    m_duoArmed = false;
    m_teamLocalReady = false;
    m_teamChallengeTarget.clear();
    m_availableDuos.clear();
    m_incomingTeamChallenges.clear();
    m_pendingTeamInviteTarget.clear();
    if (m_teamTimer != nullptr)
        m_teamTimer->stop();
    refreshPendingChallengesDisplay();

    if (m_teamDialog)
    {
        m_teamDialog->close();
        m_teamDialog = nullptr;
    }
    m_teamMembersLabel = nullptr;
    m_teamStatusLabel = nullptr;
    m_teamReadyBtn = nullptr;
    m_teamChallengeBox = nullptr;
    m_teamDuoList = nullptr;
    m_teamChallengeBtn = nullptr;
    setStatus(QStringLiteral("Saliste del equipo."));
}

void LauncherWindow::handOffTeamMatch(const QString& roomName, const QString& password,
                                       const QStringList& members, bool isMatchHost)
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString rmgkPath = exeDir + QStringLiteral("/RMG-K.exe");

    // El orden de members es el orden de asiento (P1..P4). Se lo pasamos a
    // RMG-K tal cual para que arme el live_state con los 4 nombres reales.
    QStringList args;
    args << QStringLiteral("--lobby-nickname=%1").arg(m_myNickname)
         << QStringLiteral("--lobby-match-room=%1").arg(roomName)
         << QStringLiteral("--lobby-match-password=%1").arg(password)
         << QStringLiteral("--lobby-match-type=team")
         << QStringLiteral("--lobby-team-members=%1").arg(members.join(QStringLiteral("|")))
         << QStringLiteral("--lobby-match-rom=%1").arg(romFilePath())
         << QStringLiteral("--lobby-match-rom-md5=%1").arg(m_romMd5);
    if (isMatchHost)
        args << QStringLiteral("--lobby-match-host");
    if (m_optRecord || (isMatchHost && m_optLiveReplay))
        args << QStringLiteral("--lobby-record");
    if (isMatchHost && m_optLiveReplay)
        args << QStringLiteral("--lobby-live-replay");

    leaveTeamRoom();

    // Igual que en 1v1: soltar la sala del launcher ANTES de arrancar
    // RMG-K, para que el host la pueda recrear con el mismo nombre sin
    // chocar con la version vieja que se esta por desconectar sola.
    if (m_lobbyClient != nullptr)
        m_lobbyClient->disconnectFromServer();

    m_gameProcessId = 0;
    m_gameOpponent.clear(); // Team no tiene un unico rival
    QProcess::startDetached(rmgkPath, args, exeDir, &m_gameProcessId);
    reportPresence();

    setStatus(QStringLiteral("Abriendo la partida de equipo..."));
    watchGameProcess();
}

void LauncherWindow::reconnectToLobby()
{
    if (m_lobbyClient == nullptr || m_myNickname.isEmpty() || m_lobbyConnected)
        return;

    m_connectAttempts = 0;
    m_lastConnectError.clear();
    setStatus(QStringLiteral("Volviendo al Lobby..."));
    m_lobbyClient->connectToServer(LOBBY_SERVER_URL, m_myNickname, {});
}

void LauncherWindow::enterPreGameRoom(quint64 roomId, const QString& opponentNickname, bool isHost,
                                       const QString& password, const QString& matchType)
{
    m_activeRoomId = roomId;
    m_activeOpponent = opponentNickname;
    m_activeIsHost = isHost;
    m_activeRoomPassword = password;
    m_activeMatchType = matchType;
    setSearching(false);
    m_localReady = false;
    m_remoteReady = false;

    // El nombre de la sala es lo que despues usan los dos RMG-K para
    // reencontrarse (el que retó la recrea, el otro la busca por nombre).
    const auto roomIt = m_lobbyClient->rooms().constFind(roomId);
    m_activeRoomName = (roomIt != m_lobbyClient->rooms().constEnd()) ? roomIt->name : QString();

    if (m_preGameDialog)
        m_preGameDialog->close();

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("Sala de partida"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(400, 250);

    auto* layout = new QVBoxLayout(dialog);

    const QString modeLabel = (matchType == QStringLiteral("casual"))
        ? QStringLiteral("Amistosa") : QStringLiteral("Clasificatoria");
    auto* opponentLabel = new QLabel(
        QStringLiteral("Rival: %1  ·  %2").arg(opponentNickname, modeLabel), dialog);
    opponentLabel->setStyleSheet(QStringLiteral("font-weight:700; font-size:15px;"));
    layout->addWidget(opponentLabel);

    m_preGamePingLabel = new QLabel(QStringLiteral("Ping: midiendo..."), dialog);
    layout->addWidget(m_preGamePingLabel);

    m_preGameStatusLabel = new QLabel(dialog);
    m_preGameStatusLabel->setWordWrap(true);
    layout->addWidget(m_preGameStatusLabel);
    updatePreGameStatus();

    layout->addWidget(buildRecordOptions(dialog, isHost));

    m_preGameReadyBtn = new QPushButton(QStringLiteral("Listo"), dialog);
    m_preGameReadyBtn->setObjectName("primaryBtn");
    layout->addWidget(m_preGameReadyBtn);

    auto* cancelBtn = new QPushButton(QStringLiteral("Cancelar"), dialog);
    layout->addWidget(cancelBtn);

    connect(m_preGameReadyBtn, &QPushButton::clicked, this, [this]() {
        if (m_localReady)
            return;
        m_localReady = true;
        if (m_preGameReadyBtn != nullptr)
        {
            m_preGameReadyBtn->setEnabled(false);
            m_preGameReadyBtn->setText(QStringLiteral("Esperando al rival..."));
        }
        if (m_lobbyClient != nullptr)
        {
            // Canal "room": el servidor lo entrega SOLO a los de la sala (es
            // el mismo que usa el chat de sala de RMG-K). Por el canal
            // "lobby" este mensaje tecnico le aparecería a toda la comunidad
            // en su chat.
            m_lobbyClient->sendChat(QStringLiteral("room"),
                QStringLiteral("__READY__|%1|%2").arg(m_activeRoomId).arg(m_myNickname));
        }
        updatePreGameStatus();
        if (m_remoteReady)
            handOffToGame();
    });

    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_lobbyClient != nullptr)
            m_lobbyClient->leaveRoom();
        leavePreGameRoom();
        setStatus(QStringLiteral("Saliste de la sala."));
    });

    connect(dialog, &QObject::destroyed, this, [this]() {
        m_preGamePingLabel = nullptr;
        m_preGameStatusLabel = nullptr;
        m_preGameReadyBtn = nullptr;
    });

    m_preGameDialog = dialog;
    dialog->show();

    if (m_preGamePingTimer == nullptr)
    {
        m_preGamePingTimer = new QTimer(this);
        connect(m_preGamePingTimer, &QTimer::timeout, this, [this]() {
            if (m_lobbyClient == nullptr || m_activeOpponent.isEmpty())
                return;
            for (auto it = m_lobbyClient->users().constBegin(); it != m_lobbyClient->users().constEnd(); ++it)
            {
                if (it->username == m_activeOpponent)
                {
                    m_lobbyClient->requestPingProbe(it->id);
                    break;
                }
            }
        });
    }
    // Pedido inmediato ademas del arranque del timer, para no esperar 2s a la
    // primera lectura de ping.
    for (auto it = m_lobbyClient->users().constBegin(); it != m_lobbyClient->users().constEnd(); ++it)
    {
        if (it->username == opponentNickname)
        {
            m_lobbyClient->requestPingProbe(it->id);
            break;
        }
    }
    m_preGamePingTimer->start(2000);
}

void LauncherWindow::leavePreGameRoom()
{
    if (m_preGamePingTimer != nullptr)
        m_preGamePingTimer->stop();
    m_activeRoomId = 0;
    m_activeOpponent.clear();
    m_localReady = false;
    m_remoteReady = false;
    if (m_preGameDialog)
    {
        m_preGameDialog->close();
        m_preGameDialog = nullptr;
    }
}

void LauncherWindow::updatePreGameStatus()
{
    if (m_preGameStatusLabel == nullptr)
        return;

    if (m_localReady && m_remoteReady)
        m_preGameStatusLabel->setText(QStringLiteral("Los dos estan listos! Abriendo la partida..."));
    else if (m_localReady)
        m_preGameStatusLabel->setText(QStringLiteral("Esperando a que %1 este listo...").arg(m_activeOpponent));
    else if (m_remoteReady)
        m_preGameStatusLabel->setText(QStringLiteral("%1 ya esta listo. Dale a Listo cuando quieras.").arg(m_activeOpponent));
    else
        m_preGameStatusLabel->setText(QStringLiteral("Dale a Listo cuando quieras empezar."));
}

void LauncherWindow::onLobbyPingProbeMeasured(quint64 userId, int rttMs)
{
    if (m_preGamePingLabel == nullptr || m_lobbyClient == nullptr || m_activeOpponent.isEmpty())
        return;

    const auto it = m_lobbyClient->users().constFind(userId);
    if (it == m_lobbyClient->users().constEnd() || it->username != m_activeOpponent)
        return;

    m_preGamePingLabel->setText(QStringLiteral("Ping: %1 ms").arg(rttMs));
}

void LauncherWindow::onChatSendClicked()
{
    const QString text = m_chatInput->text().trimmed();
    if (text.isEmpty() || m_lobbyClient == nullptr || !m_lobbyConnected)
        return;
    m_lobbyClient->sendChat(QStringLiteral("lobby"), text);
    m_chatInput->clear();
}

void LauncherWindow::onLobbyChatMessageReceived(const LobbyClient::ChatMessage& msg)
{
    if (msg.channel == QStringLiteral("room"))
    {
        // Mensaje tecnico de la sala previa (ver enterPreGameRoom): confirma
        // que el rival ya apreto Listo. El chat de sala no se muestra en el
        // chat general del launcher.
        if (msg.message.startsWith(QStringLiteral("__READY__|")))
        {
            const QStringList parts = msg.message.split(QLatin1Char('|'));
            if (parts.size() == 3 && parts.at(1).toULongLong() == m_activeRoomId
                && msg.fromUsername != m_myNickname)
            {
                m_remoteReady = true;
                updatePreGameStatus();
                if (m_localReady)
                    handOffToGame();
            }
        }
        // Team: el companero avisa por este canal (llega solo a los dos
        // sentados en la sala) que ya dio Listo -> el equipo queda armado.
        else if (msg.message.startsWith(QStringLiteral("__DUO_READY__|")))
        {
            const QStringList parts = msg.message.split(QLatin1Char('|'));
            if (parts.size() == 3 && parts.at(1).toULongLong() == m_teamRoomId && m_teamActive
                && !m_duoArmed && m_teamMemberNicknames.size() >= 2)
            {
                m_duoArmed = true;
                if (m_teamIsHost)
                    broadcastDuo();
                refreshTeamDialog();
            }
        }
        return;
    }

    if (msg.channel != QStringLiteral("lobby"))
        return;

    // Mensajes tecnicos del modo Team (nunca al chat visible, sean o no para
    // mi). Todos viajan por el canal "lobby" y los que no me tocan se
    // ignoran por nickname.
    if (msg.message.startsWith(QStringLiteral("__MM__|")))
    {
        const QStringList p = msg.message.split(QLatin1Char('|'));
        if (p.size() == 3 && p.at(1) != m_myNickname)
        {
            Searcher s;
            s.points = p.at(2).toDouble();
            s.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
            m_searchers.insert(p.at(1), s);
        }
        return;
    }
    if (msg.message.startsWith(QStringLiteral("__MM_OFF__|")))
    {
        const QStringList p = msg.message.split(QLatin1Char('|'));
        if (p.size() == 2)
            m_searchers.remove(p.at(1));
        return;
    }
    if (msg.message.startsWith(QStringLiteral("__DUO__|")))
    {
        const QStringList p = msg.message.split(QLatin1Char('|'));
        if (p.size() == 4 && m_teamActive && msg.fromUsername != m_myNickname)
        {
            const quint64 roomId = p.at(1).toULongLong();
            bool found = false;
            for (AvailableDuo& duo : m_availableDuos)
            {
                if (duo.roomId == roomId)
                {
                    duo.host = p.at(2);
                    duo.partner = p.at(3);
                    duo.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
                    found = true;
                    break;
                }
            }
            if (!found)
                m_availableDuos.append({roomId, p.at(2), p.at(3), QDateTime::currentMSecsSinceEpoch()});
            refreshDuoList();
        }
        return;
    }
    if (msg.message.startsWith(QStringLiteral("__DUO_OFF__|")))
    {
        const QStringList p = msg.message.split(QLatin1Char('|'));
        if (p.size() == 2)
        {
            const quint64 roomId = p.at(1).toULongLong();
            for (int i = m_availableDuos.size() - 1; i >= 0; --i)
                if (m_availableDuos.at(i).roomId == roomId)
                    m_availableDuos.removeAt(i);
            refreshDuoList();
        }
        return;
    }
    if (msg.message.startsWith(QStringLiteral("__TEAM_CHAL__|")))
    {
        // __TEAM_CHAL__|roomId|fromHost|fromPartner|toHost|toPartner
        const QStringList p = msg.message.split(QLatin1Char('|'));
        if (p.size() == 6 && p.at(4) == m_myNickname && m_teamActive && m_teamIsHost && m_duoArmed)
        {
            bool already = false;
            for (const TeamChallenge& tc : m_incomingTeamChallenges)
                if (tc.fromHost == p.at(2)) { already = true; break; }
            if (!already)
            {
                m_incomingTeamChallenges.append({p.at(2), p.at(3)});
                refreshPendingChallengesDisplay();
                notifyIncomingChallenge(p.at(2));
            }
        }
        return;
    }
    if (msg.message.startsWith(QStringLiteral("__TEAM_CHAL_NO__|")))
    {
        // __TEAM_CHAL_NO__|challengerHost|targetHost
        const QStringList p = msg.message.split(QLatin1Char('|'));
        if (p.size() == 3 && p.at(1) == m_myNickname && m_teamActive)
        {
            m_teamChallengeTarget.clear();
            setStatus(QStringLiteral("%1 rechazo tu reto.").arg(p.at(2)));
            refreshTeamDialog();
        }
        return;
    }
    if (msg.message.startsWith(QStringLiteral("__TEAM_GO__|")))
    {
        // __TEAM_GO__|room|password|P1|P2|P3|P4 -- lo manda quien acepto el
        // reto; los 4 arrancan RMG-K con la misma sala. P1 la crea.
        const QStringList p = msg.message.split(QLatin1Char('|'));
        if (p.size() == 7 && m_teamActive)
        {
            const QStringList members{p.at(3), p.at(4), p.at(5), p.at(6)};
            if (members.contains(m_myNickname))
                handOffTeamMatch(p.at(1), p.at(2), members, /*isMatchHost=*/members.at(0) == m_myNickname);
        }
        return;
    }
    if (msg.message.startsWith(QStringLiteral("__TEAM_INVITE__|")) ||
        msg.message.startsWith(QStringLiteral("__TEAM_READY__|")))
        return; // versiones anteriores del launcher: se ignoran

    appendChatLine(msg.fromUsername, msg.message, false);
}

void LauncherWindow::notifyIncomingChallenge(const QString& fromNickname)
{
    if (m_presenceMode == QStringLiteral("dnd"))
        return; // no molestar: el desafio queda en la lista, sin ruido
    // "Notification.Default" es el mismo sonido que usan los toasts nativos
    // de Windows 10/11 -- moderno y sobrio sin tener que empaquetar un
    // archivo de audio propio.
    PlaySoundW(L"Notification.Default", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);

    if (m_trayIcon != nullptr)
    {
        m_trayIcon->showMessage(
            QStringLiteral("Nuevo desafio"),
            QStringLiteral("%1 te desafio a una partida.").arg(fromNickname),
            QSystemTrayIcon::Information,
            6000);
    }

    QApplication::alert(this);
}

void LauncherWindow::appendChatLine(const QString& author, const QString& message, bool system)
{
    if (system)
    {
        m_chatLog->append(QStringLiteral("<span style='color:#5a5548;'>%1</span>")
            .arg(message.toHtmlEscaped()));
    }
    else
    {
        m_chatLog->append(QStringLiteral("<b style='color:#0ac8b9;'>%1:</b> %2")
            .arg(author.toHtmlEscaped(), message.toHtmlEscaped()));
    }
    if (QScrollBar* bar = m_chatLog->verticalScrollBar())
        bar->setValue(bar->maximum());
}
