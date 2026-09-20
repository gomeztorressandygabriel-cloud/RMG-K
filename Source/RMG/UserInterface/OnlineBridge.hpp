/*
 * Smash Remix Online Bridge
 *
 * Connects the ROM's "Ranked Account" menu (via the shared-memory struct
 * below, mirrored on the ROM side by the RMG-Audio wrapper plugin) to the
 * SmashRemix Supabase project AND to RMG-K's own rollback Lobby (the same
 * LobbyClient RollbackLobbyDialog uses), so the player can see who's online
 * and challenge them without ever leaving the game or touching a Qt dialog.
 *
 * - Player codes resolve via a security-definer Postgres RPC (never the
 *   service_role key — that key must never ship inside a binary every
 *   player runs locally).
 * - Presence/challenge reuse RMG-K's existing lobby server and LobbyClient
 *   class as-is; this class only owns a second LobbyClient instance and
 *   mirrors its state into RDRAM instead of a Qt widget.
 * - Challenges are rooms named "RETO:<target nickname>". A player's own
 *   bridge watches the room list for one addressed to their own nickname
 *   to know they've been challenged, and joins it to accept. No lobby
 *   server changes needed — this is a convention on top of existing rooms.
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

constexpr int ONLINE_BRIDGE_MAX_PRESENCE = 4;

#pragma pack(push, 1)
struct OnlineBridgeShared
{
    char     signature[12];      // "SRONLINEV1\0"
    // Inbound (written by the RMG-Audio wrapper plugin, from ROM RDRAM data)
    uint32_t command;            // 0=none, 1=resolve_code, 2=send_challenge, 3=accept_challenge
    char     inputCode[8];       // typed player code, null-terminated (resolve_code)
    char     challengeTarget[24];// nickname to challenge (send_challenge)
    uint32_t requestId;          // bumped by the plugin for every new request
    // Outbound (written by RMG-K, read back by the plugin into RDRAM)
    uint32_t responseId;         // mirrors requestId once processed
    uint32_t status;             // 0=idle,1=working,2=ok,3=not_found/failed,4=error
    char     resultNickname[24];
    // Presence: kept current continuously once connected to the lobby.
    uint32_t onlineCount;
    char     onlineNicknames[ONLINE_BRIDGE_MAX_PRESENCE][24];
    // Kept current continuously: who is challenging ME right now (empty if nobody).
    char     incomingChallenger[24];

    // Identidad de la partida de desafio actualmente en curso (ver
    // live_reader.py: la usa para reportar el resultado a Supabase via la
    // funcion report_match_result). matchKey = 0 significa "no hay ninguna
    // partida de desafio activa" (partida local/practica: no se reporta
    // nada). matchKey es el id de sala del Lobby, compartido por ambos
    // jugadores, asi que sirve para que el servidor empareje los dos
    // reportes independientes (uno por cliente) de la misma partida.
    uint32_t matchKey;
    uint32_t myPort;                    // 1-4: que puerto local juego yo en esta partida
    char     matchOpponentNickname[24];
};
#pragma pack(pop)

// command values
constexpr uint32_t ONLINE_BRIDGE_CMD_NONE = 0;
constexpr uint32_t ONLINE_BRIDGE_CMD_RESOLVE_CODE = 1;
constexpr uint32_t ONLINE_BRIDGE_CMD_SEND_CHALLENGE = 2;
constexpr uint32_t ONLINE_BRIDGE_CMD_ACCEPT_CHALLENGE = 3;

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

    // MainWindow calls this from on_Lobby_SessionRequested once the real
    // match actually starts, with the local port RollbackLobbyDialog just
    // assigned us. Finalizes the pending challenge-match identity (queued by
    // handOffRoomToLobbyDialog) into shared memory so live_reader.py can see
    // it. No-op if no challenge handoff is pending (a manual, non-ranked
    // Lobby match started instead).
    void recordMatchStarted(int localPort);

    // MainWindow calls this once emulation for a challenge-originated match
    // actually stops, so a later local/practice session isn't mistakenly
    // attributed to a stale challenge. No-op if nothing is active.
    void clearActiveChallengeMatch();

  signals:
    // A challenge room is ready to actually play: MainWindow should open the
    // real Rollback Lobby dialog and join this exact room id there, since
    // that dialog (not this headless bridge) owns the ICE/ping/match-start
    // pipeline. isHost only affects nothing functionally today (join is by
    // room id either way) but is handed along for any future UI hint.
    void challengeRoomReady(quint64 roomId, QString nickname, bool isHost);

  private slots:
    void pollSharedMemory();
    void onResolveCodeReply(QNetworkReply* reply);
    void onRankLookupReply(QNetworkReply* reply);

    // Lobby presence + rooms (mirrors Dialog::LobbyClient's own signals)
    void onLobbyPresenceChanged();
    void onLobbyStateChanged(Dialog::LobbyClient::ConnectionState state);
    void onLobbyRoomListChanged();
    void onLobbyRoomCreated(quint64 roomId);
    void onLobbyRoomCreateFailed(const QString& reason);
    void onLobbyRoomJoinOk(quint64 roomId);
    void onLobbyRoomJoinFailed(const QString& reason);

  private:
    bool openSharedMemory();
    void writeStatus(uint32_t requestId, uint32_t status, const QString& nickname = QString());
    void connectToLobbyIfNeeded();
    void refreshPresence();
    void fetchRanksForPresence(const QStringList& nicknames);
    void checkIncomingChallenge();
    void sendChallenge(uint32_t requestId, const QString& targetNickname);
    void acceptChallenge(uint32_t requestId);
    void handOffRoomToLobbyDialog(quint64 roomId, bool isHost, const QString& opponentNickname);

    HANDLE m_mapping = nullptr;
    OnlineBridgeShared* m_shared = nullptr;
    QTimer* m_pollTimer = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QNetworkAccessManager* m_rankNetwork = nullptr; // separate manager: its own "finished" signal, distinct from resolve_code
    uint32_t m_lastSeenRequestId = 0;
    bool m_requestInFlight = false;

    // Raw nicknames from the last presence refresh, kept until the rank
    // lookup replies so the display order matches what the player just saw.
    QStringList m_pendingPresenceNicknames;

    // Once a code resolves we know the player's public nickname; that's what
    // we log into the lobby with (same identity as everywhere else on the site).
    QString m_myNickname;
    Dialog::LobbyClient* m_lobbyClient = nullptr;

    // Tracks the request currently waiting on a createRoom/joinRoom result,
    // so the async roomCreated/roomJoinOk signals know which RDRAM request
    // to answer back to.
    uint32_t m_pendingRoomRequestId = 0;

    // Populated by scanning the room list for one named "RETO:<my nickname>".
    // 0 when nobody is currently challenging me.
    quint64 m_incomingChallengeRoomId = 0;
    QString m_incomingChallengerName;

    // Nickname passed to the last sendChallenge() call, so onLobbyRoomCreated
    // (which only gets a roomId back from the server) knows who the
    // challenge was actually for.
    QString m_lastChallengeTargetNickname;

    // Set by handOffRoomToLobbyDialog, finalized into m_shared by
    // recordMatchStarted once the real local port is known. 0 = nothing
    // pending.
    quint64 m_pendingMatchKey = 0;
    QString m_pendingMatchOpponent;
};

} // namespace UserInterface

#endif // NETPLAY
#endif // ONLINEBRIDGE_HPP
