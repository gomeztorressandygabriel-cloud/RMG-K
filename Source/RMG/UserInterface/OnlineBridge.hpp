/*
 * Smash Remix Online Bridge
 *
 * Connects the ROM's "Ranked" menu (via the shared-memory struct below,
 * mirrored on the ROM side by the RMG-Audio wrapper plugin) to the
 * SmashRemix Supabase project, and eventually to RollbackLobbyDialog's
 * LobbyClient to trigger matchmaking without the player leaving the game.
 *
 * v1 scope: resolve a typed player code to a public nickname/avatar via a
 * security-definer Postgres RPC (never the service_role key — that key must
 * never ship inside a binary every player runs locally).
 */
#ifndef ONLINEBRIDGE_HPP
#define ONLINEBRIDGE_HPP

#ifdef NETPLAY

#include "Dialog/Lobby/LobbyClient.hpp"

#include <QObject>
#include <QTimer>
#include <QString>
#include <cstdint>
#include <windows.h>

class QNetworkAccessManager;
class QNetworkReply;

namespace UserInterface
{

#pragma pack(push, 1)
struct OnlineBridgeShared
{
    char     signature[12];      // "SRONLINEV1\0"
    // Inbound (written by the RMG-Audio wrapper plugin, from ROM RDRAM data)
    uint32_t command;            // 0 = none, 1 = resolve_code
    char     inputCode[8];       // typed player code, null-terminated
    uint32_t requestId;          // bumped by the plugin for every new request
    // Outbound (written by RMG-K, read back by the plugin into RDRAM)
    uint32_t responseId;         // mirrors requestId once processed
    uint32_t status;             // 0 = idle, 1 = working, 2 = found, 3 = not_found, 4 = error
    char     resultNickname[24];
    // Presence: not yet populated (no Lobby connection wired up here yet),
    // but reserved now so the shared-memory layout matches wrapper.c's
    // expanded struct. Stays all-zero until a later pass connects this to
    // RollbackLobbyDialog's LobbyClient.
    uint32_t onlineCount;
    char     onlineNicknames[4][24];
};
#pragma pack(pop)

// command values
constexpr uint32_t ONLINE_BRIDGE_CMD_NONE = 0;
constexpr uint32_t ONLINE_BRIDGE_CMD_RESOLVE_CODE = 1;

// status values
constexpr uint32_t ONLINE_BRIDGE_STATUS_IDLE = 0;
constexpr uint32_t ONLINE_BRIDGE_STATUS_WORKING = 1;
constexpr uint32_t ONLINE_BRIDGE_STATUS_FOUND = 2;
constexpr uint32_t ONLINE_BRIDGE_STATUS_NOT_FOUND = 3;
constexpr uint32_t ONLINE_BRIDGE_STATUS_ERROR = 4;

class OnlineBridge : public QObject
{
    Q_OBJECT

  public:
    explicit OnlineBridge(QObject* parent = nullptr);
    ~OnlineBridge() override;

  private slots:
    void pollSharedMemory();
    void onResolveCodeReply(QNetworkReply* reply);

    // Lobby presence (mirrors Dialog::LobbyClient's own signals)
    void onLobbyPresenceChanged();
    void onLobbyStateChanged(Dialog::LobbyClient::ConnectionState state);

  private:
    bool openSharedMemory();
    void writeStatus(uint32_t requestId, uint32_t status, const QString& nickname = QString());
    void connectToLobbyIfNeeded();
    void refreshPresence();

    HANDLE m_mapping = nullptr;
    OnlineBridgeShared* m_shared = nullptr;
    QTimer* m_pollTimer = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    uint32_t m_lastSeenRequestId = 0;
    bool m_requestInFlight = false;

    // Once a code resolves we know the player's public nickname; that's what
    // we log into the lobby with (same identity as everywhere else on the site).
    QString m_myNickname;
    Dialog::LobbyClient* m_lobbyClient = nullptr;
};

} // namespace UserInterface

#endif // NETPLAY
#endif // ONLINEBRIDGE_HPP
