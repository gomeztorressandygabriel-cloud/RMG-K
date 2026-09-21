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
#include <QPoint>
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

class LauncherWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LauncherWindow(QWidget* parent = nullptr);
    ~LauncherWindow() override;

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

    void onPresenceItemDoubleClicked();
    void onPresenceContextMenuRequested(const QPoint& pos);
    void onWebsiteButtonClicked();
    void onChatSendClicked();
    void onLobbyChatMessageReceived(const LobbyClient::ChatMessage& msg);

private:
    void buildUi();
    void applyStylesheet();
    void resolveAndConnect(const QString& code);
    void checkIncomingChallenges();
    void challengePlayer(const QString& targetNickname);
    void sendChallenge(const QString& targetNickname);
    void respondToChallenge(quint64 roomId, bool accept);
    void refreshPendingChallengesDisplay();
    void handOffToGame(quint64 roomId, const QString& nickname);
    void setStatus(const QString& text);
    // Pulso de opacidad en loop, parented a `target` (se limpia solo cuando
    // el widget se destruye, p.ej. al reconstruir la lista). Se usa para
    // resaltar cualquier fila que necesite tu atencion ahora mismo.
    void startPulse(QWidget* target);

    // Roster (everyone registered on the site, not just who has the
    // launcher open) + friends.
    void fetchFullRoster();
    void refreshRosterDisplay();
    QString statusSuffixFor(const QString& nickname) const;
    void fetchFriends();
    void refreshFriendsDisplay();
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

    // ---- UI ----
    QLineEdit*   m_codeInput = nullptr;
    QPushButton* m_connectBtn = nullptr;
    QPushButton* m_websiteBtn = nullptr;
    QLabel*      m_statusLabel = nullptr;
    QLabel*      m_myNicknameLabel = nullptr;
    QListWidget* m_presenceList = nullptr;
    QListWidget* m_friendsList = nullptr;
    bool         m_lobbyConnected = false;

    // Contenedores simples (QWidget + QVBoxLayout, filas agregadas/quitadas
    // a mano) en vez de QListWidget::setItemWidget para estos dos -- ver el
    // comentario de addActionRow.
    QVBoxLayout* m_pendingLayout = nullptr;
    QVBoxLayout* m_friendRequestsLayout = nullptr;

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

    // ---- Lobby ----
    LobbyClient* m_lobbyClient = nullptr;
    QString m_myNickname;
    QString m_myCode;
    QString m_pendingChallengeTarget;

    struct PendingChallenge
    {
        quint64 roomId = 0;
        QString nickname;
    };
    // Challenges I sent, waiting for the target to join (each room stays
    // open/connected until then -- see onLobbyRoomListChanged).
    QList<PendingChallenge> m_outgoingChallenges;
    // Challenges sent to me, detected by room-naming convention
    // ("RETO:<my nickname>").
    QList<PendingChallenge> m_incomingChallenges;
};

#endif // LAUNCHERWINDOW_HPP
