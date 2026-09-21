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

using LobbyClient = UserInterface::Dialog::LobbyClient;

class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;

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

    void onChallengeButtonClicked();
    void onAcceptChallengeClicked();
    void onDeclineChallengeClicked();
    void onPresenceItemDoubleClicked();
    void onPresenceContextMenuRequested(const QPoint& pos);
    void onWebsiteButtonClicked();

private:
    void buildUi();
    void applyStylesheet();
    void resolveAndConnect(const QString& code);
    void checkIncomingChallenge();
    void sendChallenge(const QString& targetNickname);
    void handOffToGame(quint64 roomId, const QString& nickname);
    void setStatus(const QString& text);
    void showIncomingChallenge(const QString& challenger);
    void clearIncomingChallenge();

    // Roster (everyone registered on the site, not just who has the
    // launcher open) + friends.
    void fetchFullRoster();
    void refreshRosterDisplay();
    QString statusSuffixFor(const QString& nickname) const;
    void fetchFriends();
    void refreshFriendsDisplay();
    void sendFriendRequest(const QString& targetNickname);
    void respondFriendRequest(const QString& requesterNickname, bool accept);

    // ---- UI ----
    QLineEdit*   m_codeInput = nullptr;
    QPushButton* m_connectBtn = nullptr;
    QPushButton* m_websiteBtn = nullptr;
    QLabel*      m_statusLabel = nullptr;
    QLabel*      m_myNicknameLabel = nullptr;
    QListWidget* m_presenceList = nullptr;
    QLineEdit*   m_challengeTargetInput = nullptr;
    QPushButton* m_challengeBtn = nullptr;
    QListWidget* m_friendsList = nullptr;

    QWidget*     m_incomingBanner = nullptr;
    QLabel*      m_incomingLabel = nullptr;
    QPushButton* m_acceptBtn = nullptr;
    QPushButton* m_declineBtn = nullptr;

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

    // Room I created for a challenge I sent, waiting for the target to join
    // (kept connected/open until then -- see onLobbyRoomListChanged).
    quint64 m_hostedChallengeRoomId = 0;
    QString m_hostedChallengeOpponent;

    // A challenge someone else sent me, detected by room-naming convention
    // ("RETO:<my nickname>").
    quint64 m_incomingChallengeRoomId = 0;
    QString m_incomingChallengerName;
};

#endif // LAUNCHERWINDOW_HPP
