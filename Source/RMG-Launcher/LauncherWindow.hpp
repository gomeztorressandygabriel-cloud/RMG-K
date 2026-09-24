/*
 * Smash Remix Launcher - main window
 *
 * A standalone lobby client (separate .exe from RMG-K, same idea as the
 * Slippi launcher for Melee): the player saves their web account's ID once,
 * sees who else has the launcher open, and sends/receives challenges here.
 * On a matched challenge it launches RMG-K already pointed at the right
 * lobby room, so the player never sees RMG-K's own Lobby UI.
 */
#ifndef LAUNCHERWINDOW_HPP
#define LAUNCHERWINDOW_HPP

#include "Dialog/Lobby/LobbyClient.hpp"

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QList>
#include <QSet>
#include <QPoint>
#include <QPointer>
#include <QJsonObject>
#include <QPropertyAnimation>

class QStackedWidget;
class QTextEdit;
class QNetworkReply;
#include <functional>

using LobbyClient = UserInterface::Dialog::LobbyClient;

class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QTextEdit;
class QVBoxLayout;
class QNetworkAccessManager;
class QNetworkReply;
class QColor;
class QDialog;
class QTimer;
class QPropertyAnimation;
class QEvent;
class QSystemTrayIcon;
class QScrollArea;

class LauncherWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LauncherWindow(QWidget* parent = nullptr);
    ~LauncherWindow() override;

    // prince://watch?nick=<jugador>: lo manda la pagina web (boton "Ver en
    // vivo") o main.cpp cuando el launcher se abre con esa direccion.
    void handleWatchUrl(const QString& url);

private slots:
    void onConnectClicked();
    void onResolveCodeReply(QNetworkReply* reply);
    void onFullRosterReply(QNetworkReply* reply);
    void onSendFriendRequestReply(QNetworkReply* reply);
    void onRespondFriendRequestReply(QNetworkReply* reply);
    void onListFriendsReply(QNetworkReply* reply);

    void onLobbyStateChanged(LobbyClient::ConnectionState state);
    void onLobbyHelloFailed(const QString& reason);
    void onLobbyConnectError(const QString& message);
    void onLobbyPresenceChanged();
    void onLobbyRoomListChanged();
    void onLobbyRoomCreated(quint64 roomId);
    void onLobbyRoomCreateFailed(const QString& reason);
    void onLobbyRoomJoinOk(quint64 roomId);
    void onLobbyRoomJoinFailed(const QString& reason);
    void onLobbyPingProbeMeasured(quint64 userId, int rttMs);

    void onPresenceItemDoubleClicked();
    void onPresenceContextMenuRequested(const QPoint& pos);
    void onWebsiteButtonClicked();
    void onCreateShortcutClicked();
    void onCheckUpdateClicked();
    void onChatSendClicked();
    void onLobbyChatMessageReceived(const LobbyClient::ChatMessage& msg);

private:
    void buildUi();
    void applyStylesheet();
    void resolveAndConnect(const QString& code);
    void checkIncomingChallenges();
    void challengePlayer(const QString& targetNickname, const QString& matchType);
    void sendChallenge(const QString& targetNickname, const QString& matchType);
    void respondToChallenge(quint64 roomId, bool accept);
    void refreshPendingChallengesDisplay();

    // ---- Team (2 equipos de 2): P1+P2 arma el grupo, P3+P4 son los
    // invitados individualmente. Ver LiveStatsReporter::determineWinningTeam
    // en RMG-K para la misma convencion de asientos. ----
    //
    // La PRIMERA invitacion (armar el par) crea una sala nueva con el
    // nombre del invitado embebido ("TEAM:<nick>:<password>"), igual que un
    // desafio 1v1 -- el invitado la detecta escaneando salas, mismo
    // mecanismo probado con checkIncomingChallenges().
    void inviteToTeam(const QString& targetNickname);
    void checkIncomingTeamInvites();
    void respondToTeamInvite(quint64 roomId, bool accept, const QString& password,
                             const QString& fromNickname);
    void refreshTeamDialog();
    void leaveTeamRoom();
    // Equipo de 2: cuando el companero da Listo el equipo queda "armado" y el
    // anfitrion ve la lista de otros equipos armados para retarlos.
    void disarmDuo();
    void broadcastDuo();
    void broadcastDuoOff();
    void refreshDuoList();
    void teamTick();
    void challengeDuo(const QString& targetHost, const QString& targetPartner);
    void respondToTeamChallenge(const QString& fromHost, bool accept);
    void handOffTeamMatch(const QString& roomName, const QString& password,
                          const QStringList& members, bool isMatchHost);
    // Comun a handOffToGame() y handOffTeamMatch(): detecta cuando RMG-K se
    // cierra y vuelve solo al Lobby.
    void watchGameProcess();
    // Casillas "Grabar partida" / "Live Replay" de las salas previas. Live
    // Replay solo aparece para el host (el servidor rechaza a cualquier otro).
    QWidget* buildRecordOptions(QWidget* parent, bool isHost);
    void startWatching(const QString& targetNickname);
    // Cierra la sesion de esta cuenta: olvida el codigo guardado y vuelve a
    // la pantalla de "escribi tu codigo".
    void logOut();
    bool m_optRecord = false;
    bool m_optLiveReplay = false;
    QString m_pendingWatchNick;
    // Lanza RMG-K ya con el rol y la sala resueltos. No se le pasa el id de
    // la sala del launcher a proposito: esa sala muere apenas los dos
    // launchers se desconectan (el servidor borra la sala que se queda sin
    // nadie), asi que RMG-K no tendria nada a donde entrar. En su lugar cada
    // RMG-K recrea la sala por nombre: el que reto la crea, el otro la busca.
    void handOffToGame();
    // Ruta y MD5 de la ROM modificada que se distribuye junto al launcher.
    // Se le pasa explicita a RMG-K porque su biblioteca de ROMs apunta a otra
    // carpeta y ahi solo esta la Smash Remix normal -- sin esto arrancaria la
    // ROM equivocada, o ninguna.
    QString romFilePath() const;
    // Vuelve a conectarse al Lobby con la cuenta que ya estaba resuelta.
    // Se usa al cerrar el juego: el launcher se habia desconectado para
    // cederle la sala a RMG-K y quedaba inservible hasta reabrirlo.
    void reconnectToLobby();
    // Publica en la web si estoy conectado y contra quien estoy jugando.
    // Cada jugador escribe solo su propia fila, asi que no hay nada que se
    // pise entre clientes (a diferencia del estado en vivo de la partida).
    void reportPresence();
    // Sala previa a la partida: los dos jugadores ven ping en vivo y confirman
    // "Listo" cada uno por su lado (via un mensaje tecnico en el chat de
    // lobby ya probado en vivo); recien cuando ambos confirmaron se lanza
    // RMG-K. isHost no cambia el comportamiento hoy (ambos lados esperan lo
    // mismo) pero se deja para diferenciar mas adelante si hace falta.
    void enterPreGameRoom(quint64 roomId, const QString& opponentNickname, bool isHost,
                          const QString& password, const QString& matchType);
    void leavePreGameRoom();
    void updatePreGameStatus();
    void setStatus(const QString& text);
    // Pulso de opacidad en loop, parented a `target` (se limpia solo cuando
    // el widget se destruye, p.ej. al reconstruir la lista). Se usa para
    // resaltar cualquier fila que necesite tu atencion ahora mismo.
    void startPulse(QWidget* target);

    // Roster (everyone registered on the site, not just who has the
    // launcher open) + friends.
    void fetchFullRoster();
    void refreshRosterDisplay();
    // Estado que reporta el Lobby para ese nickname ("idle", "playing", ...),
    // o cadena vacia si no esta conectado.
    QString lobbyStateFor(const QString& nickname) const;
    void fetchFriends();
    void refreshFriendsDisplay();
    // Nivel de experiencia (sin tope): 100 XP por partida + 50 por victoria.
    // La misma cuenta esta en la web (config.js, levelInfo).
    void fetchLevels();
    void onLevelsReply(QNetworkReply* reply);
    void refreshMyLevelLabel();
    QString levelSuffix(const QString& nickname) const;
    // ---- Estado de presencia, matchmaking, revancha, diagnostico ----
    void setPresenceMode(const QString& mode);
    void setSearching(bool on);
    void searchTick();
    void offerRematch(const QString& opponent, const QString& matchType);
    void showDiagnostics();
    void showWelcomeIfFirstRun();
    void showPendingUpdateNotes();
    void fetchHeadToHead(const QString& nickname);
    void onH2hReply(QNetworkReply* reply);
    double rankPointsFor(const QString& nickname) const;
    void showListTab(bool friends);
    void onFriendContextMenuRequested(const QPoint& pos);
    void onFriendsTick();
    bool isBroadcastingPlayer(const QString& nickname) const;
    // Aviso con sonido + globo de la bandeja + parpadeo de la barra de tareas.
    void notifyToast(const QString& title, const QString& text);
    // Chat privado (solo entre amigos). Los mensajes viajan por Supabase, no
    // por el chat del Lobby, asi que nadie mas puede leerlos.
    void openDirectChat(const QString& nickname);
    void fetchDm(const QString& nickname);
    void sendDm(const QString& nickname, const QString& text);
    void fetchUnreadDms();
    void onDmListReply(QNetworkReply* reply);
    void onDmSendReply(QNetworkReply* reply);
    void onDmUnreadReply(QNetworkReply* reply);
    QNetworkReply* postFriendsRpc(const QString& function, const QJsonObject& body, const char* op);
    void sendFriendRequest(const QString& targetNickname);
    void respondFriendRequest(const QString& requesterNickname, bool accept);

    // Fila con texto + Aceptar/Rechazar, usada tanto para desafios como para
    // solicitudes de amistad. `layout` es un QVBoxLayout simple (no
    // QListWidget::setItemWidget -- esa tecnica broke en la primera prueba
    // en vivo con dos cuentas reales y de nuevo despues de un primer
    // intento de arreglo; un QWidget/QVBoxLayout comun es el camino mas
    // probado en Qt para filas con botones).
    void addActionRow(QVBoxLayout* layout, const QString& text, const QString& textColor,
                       std::function<void()> onAccept, std::function<void()> onDecline,
                       bool pulse);
    void addPlainRow(QVBoxLayout* layout, const QString& text, const QColor& color);
    // Vacia `layout` (borra sus widgets hijos) antes de reconstruirlo.
    void clearLayout(QVBoxLayout* layout);
    void appendChatLine(const QString& author, const QString& message, bool system);
    // Notificacion (globo del sistema + sonido) para un desafio recien
    // recibido -- la fila pulsante en DESAFIOS PENDIENTES se puede pasar
    // por alto si la ventana no esta al frente.
    void notifyIncomingChallenge(const QString& fromNickname);

    // ---- UI ----
    QLineEdit*   m_codeInput = nullptr;
    QPushButton* m_connectBtn = nullptr;
    QPushButton* m_websiteBtn = nullptr;
    QPushButton* m_shortcutBtn = nullptr;
    QPushButton* m_updateBtn = nullptr;
    QLabel*      m_statusLabel = nullptr;
    QLabel*      m_myNicknameLabel = nullptr;
    QWidget*     m_accountCard = nullptr;
    QLabel*      m_avatarLabel = nullptr;
    // Estado visible mientras todavia no hay cuenta (la tarjeta esta oculta).
    QLabel*      m_preLoginStatus = nullptr;
    QListWidget* m_presenceList = nullptr;
    QListWidget* m_friendsList = nullptr;
    bool         m_lobbyConnected = false;

    // Contenedores simples (QWidget + QVBoxLayout, filas agregadas/quitadas
    // a mano) en vez de QListWidget::setItemWidget para estos dos -- ver el
    // comentario de addActionRow.
    QVBoxLayout* m_pendingLayout = nullptr;
    QVBoxLayout* m_friendRequestsLayout = nullptr;

    // Cada "seccion" agrupa su titulo + su lista, para poder ocultarla
    // entera cuando no tiene nada: un titulo sobre un recuadro vacio solo
    // gasta espacio y aplana la jerarquia visual del panel.
    QWidget*     m_pendingSection = nullptr;
    QScrollArea* m_pendingScroll = nullptr;
    QWidget*     m_friendRequestsSection = nullptr;
    QScrollArea* m_friendRequestsScroll = nullptr;
    QWidget*     m_friendsSection = nullptr;
    QWidget*     m_accountBox = nullptr;
    // "3 en linea" al lado del titulo JUGADORES.
    QLabel*      m_onlineCountLabel = nullptr;

    // Latido del punto de quien esta en partida. Se hace repintando solo el
    // icono de esas filas (no con QGraphicsEffect): los efectos anidados ya
    // nos rompieron el aviso de desafios una vez y no vale la pena el riesgo.
    QTimer* m_dotPulseTimer = nullptr;
    bool    m_dotPulseOn = false;

    QTextEdit*   m_chatLog = nullptr;
    QLineEdit*   m_chatInput = nullptr;
    QPushButton* m_chatSendBtn = nullptr;

    // ---- Networking (Supabase) ----
    QNetworkAccessManager* m_network = nullptr;
    QNetworkAccessManager* m_rosterNetwork = nullptr;
    QNetworkAccessManager* m_friendsNetwork = nullptr;

    struct RosterEntry
    {
        QString nickname;
        double rankPoints = 0;
    };
    QList<RosterEntry> m_fullRoster;

    struct FriendEntry
    {
        QString nickname;
        QString status;       // "pending" or "accepted"
        bool iAmRequester = false;
    };
    QList<FriendEntry> m_friends;
    QHash<QString, qint64> m_xp; // nickname -> XP total
    QString m_presenceMode = QStringLiteral("online"); // online | dnd | searching
    QString m_modeBeforeSearch = QStringLiteral("online");
    bool    m_searching = false;
    qint64  m_searchStartMs = 0;
    qint64  m_mmChallengeMs = 0;
    int     m_searchTickCount = 0;
    QTimer* m_searchTimer = nullptr;
    struct Searcher { double points = 0; qint64 lastSeenMs = 0; };
    QHash<QString, Searcher> m_searchers;
    QString m_mmChallenged;
    QPushButton* m_searchBtn = nullptr;
    QPushButton* m_rematchBtn = nullptr;
    QString m_rematchOpponent;
    QString m_rematchType;
    QString m_lastMatchType = QStringLiteral("ranked");
    QHash<QString, QString> m_h2hCache;
    QString m_updateNotes;
    QPushButton*   m_tabPlayersBtn = nullptr;
    QPushButton*   m_tabFriendsBtn = nullptr;
    QStackedWidget* m_listStack = nullptr;
    QPropertyAnimation* m_friendsScrollAnim = nullptr;
    QTimer* m_friendsTimer = nullptr;
    int  m_friendsTick = 0;
    int  m_friendsErrors = 0;
    bool m_friendsLoadedOnce = false;
    QSet<QString> m_knownIncomingRequests;
    QSet<QString> m_knownOutgoing;
    QHash<QString, QPointer<QDialog>> m_dmDialogs;
    QHash<QString, QTextEdit*> m_dmLogs;
    QHash<QString, qint64> m_dmLastId;
    QHash<QString, int> m_dmUnread;
    QHash<QString, qint64> m_dmNotifiedId;

    // ---- Lobby ----
    LobbyClient* m_lobbyClient = nullptr;
    QString m_myNickname;
    QString m_myCode;
    QString m_pendingChallengeTarget;
    // Nombre del rival cuya sala estoy por unirme (respondToChallenge ya
    // saco la entrada de m_incomingChallenges antes de que llegue
    // onLobbyRoomJoinOk, asi que hay que guardarlo aparte).
    QString m_pendingJoinOpponent;

    struct PendingChallenge
    {
        quint64 roomId = 0;
        QString nickname;
        // Solo se usa del lado de quien recibe el desafio (para el joinRoom);
        // va embebida en el nombre de la sala -- ver CHALLENGE_ROOM_PREFIX.
        QString password;
        // "ranked" o "casual" -- tambien va embebido en el nombre de la sala.
        QString matchType = QStringLiteral("ranked");
    };
    // Challenges I sent, waiting for the target to join (each room stays
    // open/connected until then -- see onLobbyRoomListChanged).
    QList<PendingChallenge> m_outgoingChallenges;
    // Challenges sent to me, detected by room-naming convention
    // ("RETO:<my nickname>:<password>").
    QList<PendingChallenge> m_incomingChallenges;

    // ---- Sala previa a la partida (ping + Listo/Listo) ----
    QPointer<QDialog> m_preGameDialog;
    QLabel*      m_preGamePingLabel = nullptr;
    QLabel*      m_preGameStatusLabel = nullptr;
    QPushButton* m_preGameReadyBtn = nullptr;
    QTimer*      m_preGamePingTimer = nullptr;
    quint64      m_activeRoomId = 0;
    QString      m_activeOpponent;
    QString      m_activeRoomName;
    QString      m_activeRoomPassword;
    // "ranked" o "casual" -- decide si el resultado toca puntos/wins/losses
    // o solo queda en el historial. Ver report_match_result en la base.
    QString      m_activeMatchType = QStringLiteral("ranked");
    bool         m_activeIsHost = false;
    bool         m_localReady = false;
    bool         m_remoteReady = false;

    // MD5 real del ssb64asm.z64 que tiene ESTE jugador al lado del launcher.
    // Se calcula al arrancar en vez de hardcodearlo: si alguien tiene una
    // build distinta de la ROM, las salas no coinciden y el desafio falla
    // limpio en vez de desincronizarse dentro de la partida.
    QString m_romMd5;

    // Datos del desafio que estoy por mandar / por aceptar, hasta que la sala
    // exista y se pueda pasar a la sala previa.
    QString m_pendingChallengePassword;
    QString m_pendingChallengeMatchType = QStringLiteral("ranked");
    QString m_pendingJoinPassword;
    QString m_pendingJoinMatchType = QStringLiteral("ranked");

    // ---- Team (2 equipos de 2) ----
    struct PendingTeamInvite
    {
        quint64 roomId = 0;
        QString fromNickname;
        QString password;
    };
    // Primera invitacion (arma el par), detectada por nombre de sala --
    // mismo mecanismo que m_incomingChallenges.
    QList<PendingTeamInvite> m_incomingTeamInvites;

    bool     m_creatingTeamRoom = false; // el proximo ROOM_CREATED es de team, no de desafio
    QString  m_pendingTeamPassword;
    QString  m_pendingTeamInviteTarget;

    quint64  m_teamRoomId = 0;
    QString  m_teamRoomName;
    QString  m_teamRoomPassword;
    bool     m_teamIsHost = false;  // solo el host puede invitar a los 2 que faltan
    bool     m_teamActive = false;  // estoy en una sala de team (formando o completa)
    QStringList m_teamMemberNicknames;
    bool     m_duoArmed = false;   // el companero ya dio Listo: equipo armado
    struct AvailableDuo
    {
        quint64 roomId = 0;
        QString host;
        QString partner;
        qint64  lastSeenMs = 0;
    };
    QList<AvailableDuo> m_availableDuos;      // equipos armados que vi anunciarse
    struct TeamChallenge
    {
        QString fromHost;
        QString fromPartner;
    };
    QList<TeamChallenge> m_incomingTeamChallenges;
    QString  m_teamChallengeTarget;           // anfitrion al que reto (esperando)
    QTimer*  m_teamTimer = nullptr;
    int      m_teamTickCount = 0;
    QWidget*     m_teamChallengeBox = nullptr;
    QListWidget* m_teamDuoList = nullptr;
    QPushButton* m_teamChallengeBtn = nullptr;
    bool     m_teamLocalReady = false;

    QPointer<QDialog> m_teamDialog;
    QLabel*      m_teamMembersLabel = nullptr;
    QLabel*      m_teamStatusLabel = nullptr;
    QPushButton* m_teamReadyBtn = nullptr;

    // RMG-K se lanza suelto (no como hijo) a proposito: si fuera un QProcess
    // hijo, cerrar el launcher durante una partida mataria el juego. Por eso
    // se sigue por PID en vez de por la señal finished().
    qint64  m_gameProcessId = 0;
    QTimer* m_gameWatchTimer = nullptr;
    // Rival de la partida en curso, para poder publicar "jugando contra X"
    // mientras el juego corre.
    QString m_gameOpponent;

    QNetworkAccessManager* m_presenceNetwork = nullptr;
    QTimer* m_presenceTimer = nullptr;

    // ---- Actualizaciones ----
    // Solo se descarga lo que cambio de verdad. La ROM (65 MB) y las DLL de
    // Qt no cambian casi nunca; lo que se actualiza en cada arreglo son un
    // par de ejecutables de pocos MB.
    struct UpdateFile
    {
        QString path;    // ruta relativa dentro de la carpeta del juego
        QString url;     // de donde bajarlo (GitHub Releases)
        QString sha256;  // para verificar que llego entero
    };
    QNetworkAccessManager* m_updateNetwork = nullptr;
    QList<UpdateFile> m_updateQueue;
    int      m_updateIndex = 0;
    QString  m_updateVersion;
    QString  m_updateStageDir;
    void checkForUpdates();
    void downloadNextUpdateFile();
    // Al abrir: primero se busca actualizacion (launcher + RMG-K + plugins) y
    // recien despues se conecta la cuenta guardada, para que nadie juegue con
    // un RMG-K viejo. Si no hay red o no hay nada nuevo, sigue de largo.
    bool m_startupUpdateCheck = false;
    void startAutoConnect();
    void finishUpdateFlow();
    void applyStagedUpdate();
    void setUpdateStatus(const QString& text, bool error = false);

protected:
    void closeEvent(QCloseEvent* event) override;
    // Intercepta la rueda del mouse sobre la lista de jugadores para animar
    // el desplazamiento en vez de saltar de golpe.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPropertyAnimation* m_scrollAnim = nullptr;

    QSystemTrayIcon* m_trayIcon = nullptr;

    // El servidor limita conexiones nuevas por IP en ventanas cortas
    // (responde 429 con "Retry-After: 3s"). Dos launchers abiertos a la vez,
    // o reabrir uno enseguida despues de cerrarlo, alcanzan para chocar con
    // ese limite; sin reintento el launcher se quedaba muerto en
    // "No se pudo conectar" para siempre. Reintento acotado y espaciado --
    // nunca un bucle.
    int     m_connectAttempts = 0;
    QString m_lastConnectError;
};

#endif // LAUNCHERWINDOW_HPP
