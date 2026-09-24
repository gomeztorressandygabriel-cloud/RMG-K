#include "LiveStatsReporter.hpp"
#include "OnlineBridge.hpp"

#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDateTime>
#include <QUuid>
#include <QEventLoop>

#include <windows.h>

namespace
{
// Layout publicado por el wrapper del plugin de audio (ver la cabecera de
// live_reader.py). Los offsets son parte del contrato con el ROM parchado
// (LiveReport.asm): si cambian alla, cambian aca.
constexpr char    SHARED_MAP_NAME[] = "SmashRemixLiveReport";
constexpr quint32 HEADER_SIZE = 0x28;
constexpr quint32 PLAYER_BLOCK_SIZE = 0x38;
constexpr int     NUM_PLAYERS = 4;
constexpr quint32 TOTAL_SIZE = HEADER_SIZE + PLAYER_BLOCK_SIZE * NUM_PLAYERS;

// en curso, pausado, despausando, fin de partida, mostrando resultado
inline bool isActiveStatus(quint32 status)
{
    return status == 1 || status == 2 || status == 3 || status == 5 || status == 6;
}

// Misma anon key publica que usa la web y el resto del cliente -- nunca la
// service_role. report_match_result valida todo del lado del servidor.
constexpr char SUPABASE_URL[] = "https://jexxpiumbulpuashtjia.supabase.co";
constexpr char SUPABASE_ANON_KEY[] = "sb_publishable_AS3AbHJZ-nMJsdRkZbXFAA_M6aeTWVB";

quint32 readU32(const unsigned char* p, quint32 offset)
{
    quint32 v = 0;
    memcpy(&v, p + offset, sizeof(v));
    return v;
}
} // namespace

LiveStatsReporter::LiveStatsReporter(OnlineBridge* bridge, QObject* parent)
    : QObject(parent), m_bridge(bridge)
{
    m_network = new QNetworkAccessManager(this);
    // Identifica a este cliente para el turno de emision en vivo. Uno nuevo
    // por sesion: si el juego se reabre, el turno viejo caduca solo.
    m_liveToken = QUuid::createUuid().toString(QUuid::WithoutBraces);

    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &LiveStatsReporter::poll);
    m_timer->start();
}

LiveStatsReporter::~LiveStatsReporter()
{
    if (m_view != nullptr)
    {
        UnmapViewOfFile(m_view);
    }
    if (m_mapping != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(m_mapping));
    }
}

bool LiveStatsReporter::openSharedMemory()
{
    if (m_view != nullptr)
    {
        return true;
    }

    // OpenFileMapping y no CreateFileMapping: si el plugin de audio todavia
    // no publico nada, no hay nada que leer y se reintenta en el proximo
    // tick. Crearla nosotros solo dejaria un bloque de ceros.
    HANDLE mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, SHARED_MAP_NAME);
    if (mapping == nullptr)
    {
        return false;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, TOTAL_SIZE);
    if (view == nullptr)
    {
        CloseHandle(mapping);
        return false;
    }

    m_mapping = mapping;
    m_view = static_cast<const unsigned char*>(view);
    return true;
}

bool LiveStatsReporter::readSnapshot(Snapshot& out) const
{
    if (m_view == nullptr || memcmp(m_view, "SM2SCANV1", 9) != 0)
    {
        return false;
    }

    out.gameStatus = readU32(m_view, 0x0C + 4);
    out.stage = readU32(m_view, 0x0C + 12);
    out.elapsedSeconds = readU32(m_view, 0x0C + 20) / 60.0;

    for (int i = 0; i < NUM_PLAYERS; ++i)
    {
        const quint32 base = HEADER_SIZE + i * PLAYER_BLOCK_SIZE;
        PlayerStats& p = out.players[i];
        p.active = readU32(m_view, base) != 0;
        p.characterId = readU32(m_view, base + 4);
        p.damage = readU32(m_view, base + 8);
        p.stock = readU32(m_view, base + 12);
        p.totalDealt = readU32(m_view, base + 16);
        p.totalTaken = readU32(m_view, base + 20);
        p.highestHitTaken = readU32(m_view, base + 24);
        p.currentCombo = readU32(m_view, base + 28);
        p.maxComboHitsTaken = readU32(m_view, base + 32);
        p.maxComboDamageTaken = readU32(m_view, base + 36);
        p.maxComboDealt = readU32(m_view, base + 40);
        p.kos = readU32(m_view, base + 44);
        p.deaths = readU32(m_view, base + 48);
        // 0 o >=256 (desborde del byte de vidas al perder la ultima) = eliminado
        if (p.stock >= 256)
        {
            p.stock = 0;
        }
    }

    return true;
}

int LiveStatsReporter::determineWinnerPort(const Snapshot& snapshot) const
{
    // Regla principal, la que decide casi todas las partidas: el que se
    // queda sin vidas perdio. Si queda exactamente un jugador con vidas, ese
    // gano y no hace falta mirar nada mas.
    {
        int alivePort = 0;
        int aliveCount = 0;
        int activeCount = 0;
        for (int i = 1; i <= NUM_PLAYERS; ++i)
        {
            if (!snapshot.players[i - 1].active)
            {
                continue;
            }
            ++activeCount;
            if (snapshot.players[i - 1].stock > 0)
            {
                ++aliveCount;
                alivePort = i;
            }
        }
        if (activeCount >= 2 && aliveCount == 1)
        {
            return alivePort;
        }
    }

    // Sin exactamente un jugador con vidas no hay resultado: pierde
    // UNICAMENTE quien llega a 0 vidas. Si se reinicia la partida, se acaba
    // el tiempo o los dos quedan en 0, no se reporta nada (antes se
    // desempataba por daño y le daba la derrota a quien tenia menos).
    return 0;
}

int LiveStatsReporter::determineWinningTeam(const Snapshot& snapshot) const
{
    // Equipos por asiento (convencion estandar de Smash): P1+P2 = equipo 1,
    // P3+P4 = equipo 2. Mismo criterio que determineWinnerPort: el equipo
    // que se queda sin nadie con vidas perdio.
    auto teamAlive = [&snapshot](int first, int second) {
        const bool a = snapshot.players[first - 1].active && snapshot.players[first - 1].stock > 0;
        const bool b = snapshot.players[second - 1].active && snapshot.players[second - 1].stock > 0;
        return a || b;
    };
    const bool team1Alive = teamAlive(1, 2);
    const bool team2Alive = teamAlive(3, 4);
    if (team1Alive && !team2Alive)
        return 1;
    if (team2Alive && !team1Alive)
        return 2;

    // Un equipo pierde UNICAMENTE cuando ninguno de sus integrantes tiene
    // vidas. Si los dos siguen en pie (reinicio, tiempo) o los dos quedaron
    // en cero, no se reporta nada.
    return 0;
}

void LiveStatsReporter::reportMatch(const Snapshot& snapshot)
{
    if (m_bridge == nullptr)
    {
        return;
    }

    OnlineBridge::ActiveMatchIdentity identity;
    if (!m_bridge->activeMatchIdentity(identity))
    {
        return; // partida local/practica: no va a la pagina
    }

    if (identity.myPort < 1 || identity.myPort > NUM_PLAYERS)
    {
        return; // puerto propio invalido: no se puede saber que fue mio
    }

    const bool isTeam = (identity.matchType == QStringLiteral("team"));

    // Mis propios numeros de esta partida. Sin esto la fila de match_players
    // no se crea y la web se queda sin historial, enfrentamientos directos,
    // personaje favorito, estadisticas ni records.
    const PlayerStats& me = snapshot.players[identity.myPort - 1];
    QJsonObject stats;
    stats["port"] = static_cast<int>(identity.myPort);
    stats["character_id"] = static_cast<int>(me.characterId);
    stats["damage"] = static_cast<int>(me.damage);
    stats["stock"] = static_cast<int>(me.stock);
    stats["kos"] = static_cast<int>(me.kos);
    stats["deaths"] = static_cast<int>(me.deaths);
    stats["total_dealt"] = static_cast<int>(me.totalDealt);
    stats["total_taken"] = static_cast<int>(me.totalTaken);
    stats["highest_hit_taken"] = static_cast<int>(me.highestHitTaken);
    stats["max_combo_hits_taken"] = static_cast<int>(me.maxComboHitsTaken);
    stats["max_combo_damage_taken"] = static_cast<int>(me.maxComboDamageTaken);
    stats["max_combo_dealt"] = static_cast<int>(me.maxComboDealt);

    QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/rpc/%2")
        .arg(QLatin1String(SUPABASE_URL),
             isTeam ? QStringLiteral("report_team_match_result") : QStringLiteral("report_match_result"))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);

    QJsonObject body;

    if (isTeam)
    {
        const int winningTeam = determineWinningTeam(snapshot);
        if (winningTeam == 0)
        {
            return;
        }
        // Equipos por asiento: P1+P2 = 1, P3+P4 = 2 (ver determineWinningTeam).
        const int myTeam = (identity.myPort <= 2) ? 1 : 2;
        const QString result = (myTeam == winningTeam) ? QStringLiteral("win") : QStringLiteral("loss");

        // Cada uno de los 4 reporta SOLO lo suyo. El servidor junta las 4
        // filas antes de aplicar nada (ver report_team_match_result).
        body["p_match_key"] = QString::number(identity.matchKey);
        body["p_my_nickname"] = identity.myNickname;
        body["p_team"] = myTeam;
        body["p_port"] = static_cast<int>(identity.myPort);
        body["p_result"] = result;
        body["p_stage_id"] = static_cast<int>(snapshot.stage);
        body["p_duration_seconds"] = static_cast<int>(snapshot.elapsedSeconds);
        body["p_my_stats"] = stats;
    }
    else
    {
        const int winnerPort = determineWinnerPort(snapshot);
        if (winnerPort == 0)
        {
            return;
        }
        const QString result = (static_cast<quint32>(winnerPort) == identity.myPort)
            ? QStringLiteral("win") : QStringLiteral("loss");

        // Cada cliente reporta SOLO lo suyo. El servidor aplica wins/losses y
        // puntos recien cuando le llego tambien el reporte del rival con la
        // misma match_key y los dos coinciden.
        body["p_match_key"] = QString::number(identity.matchKey);
        body["p_my_nickname"] = identity.myNickname;
        body["p_opponent_nickname"] = identity.opponentNickname;
        body["p_result"] = result;
        body["p_stage_id"] = static_cast<int>(snapshot.stage);
        body["p_duration_seconds"] = static_cast<int>(snapshot.elapsedSeconds);
        body["p_my_stats"] = stats;
        body["p_match_type"] = identity.matchType;
    }

    QNetworkReply* reply = m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void LiveStatsReporter::pushLiveState(const Snapshot& snapshot)
{
    if (m_bridge == nullptr)
    {
        return;
    }

    OnlineBridge::ActiveMatchIdentity identity;
    if (!m_bridge->activeMatchIdentity(identity))
    {
        return; // solo se transmiten las partidas de desafio
    }

    const bool isTeam = (identity.matchType == QStringLiteral("team"));

    // La pagina muestra nombres, no "Puerto N". Fuera de Team se conoce el
    // mio por mi puerto, y en un 1v1 el otro activo es necesariamente el
    // rival. En Team el launcher ya nos dio los 4 nombres en orden de
    // asiento (P1..P4), asi que cada puerto tiene su propio nombre real en
    // vez de que 3 de los 4 jugadores salgan en blanco.
    QJsonArray players;
    for (int i = 1; i <= NUM_PLAYERS; ++i)
    {
        const PlayerStats& p = snapshot.players[i - 1];
        if (!p.active)
        {
            continue;
        }
        QString nickname;
        if (isTeam)
        {
            nickname = (i - 1 < identity.teamNicknames.size()) ? identity.teamNicknames[i - 1]
                       : (static_cast<quint32>(i) == identity.myPort ? identity.myNickname : QString());
        }
        else if (static_cast<quint32>(i) == identity.myPort)
        {
            nickname = identity.myNickname;
        }
        else
        {
            nickname = identity.opponentNickname;
        }

        QJsonObject obj;
        obj["port"] = i;
        obj["active"] = true;
        obj["nickname"] = nickname;
        obj["character_id"] = static_cast<int>(p.characterId);
        obj["damage"] = static_cast<int>(p.damage);
        obj["stock"] = static_cast<int>(p.stock);
        obj["current_combo"] = static_cast<int>(p.currentCombo);
        obj["total_dealt"] = static_cast<int>(p.totalDealt);
        obj["total_taken"] = static_cast<int>(p.totalTaken);
        // Convencion de asiento acordada: P1+P2 = equipo 1, P3+P4 = equipo 2.
        if (isTeam)
        {
            obj["team"] = (i <= 2) ? 1 : 2;
        }
        players.append(obj);
    }

    QJsonObject body;
    body["p_token"] = m_liveToken;
    body["p_match_key"] = QString::number(identity.matchKey);
    body["p_is_live"] = true;
    body["p_game_status"] = static_cast<int>(snapshot.gameStatus);
    body["p_stage_id"] = static_cast<int>(snapshot.stage);
    body["p_elapsed_seconds"] = static_cast<int>(snapshot.elapsedSeconds);
    body["p_players"] = players;
    body["p_match_type"] = identity.matchType;
    body["p_live_replay"] = m_bridge->liveReplayActive();

    QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/rpc/push_live_state")
        .arg(QLatin1String(SUPABASE_URL))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);

    QNetworkReply* reply = m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    m_livePushActive = true;
}

void LiveStatsReporter::releaseLiveState()
{
    if (!m_livePushActive)
    {
        return;
    }
    m_livePushActive = false;

    QJsonObject body;
    body["p_token"] = m_liveToken;

    QNetworkRequest req(QUrl(QStringLiteral("%1/rest/v1/rpc/release_live_state")
        .arg(QLatin1String(SUPABASE_URL))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("apikey", SUPABASE_ANON_KEY);
    req.setRawHeader("Authorization", QByteArray("Bearer ") + SUPABASE_ANON_KEY);

    QNetworkReply* reply = m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void LiveStatsReporter::finalizePendingMatch()
{
    if (!m_haveLastActive || m_matchProcessed)
    {
        releaseLiveState();
        return;
    }

    Snapshot corrected = m_lastActive;
    for (int i = 1; i <= NUM_PLAYERS; ++i)
    {
        corrected.players[i - 1].stock = m_minStock.value(i, corrected.players[i - 1].stock);
    }
    m_matchProcessed = true;

    releaseLiveState();
    reportMatch(corrected);

    // El proceso esta por morir: sin esperar, el POST queda a medio salir y
    // el resultado se pierde igual que antes.
    QEventLoop loop;
    QTimer::singleShot(2500, &loop, &QEventLoop::quit);
    connect(m_network, &QNetworkAccessManager::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

void LiveStatsReporter::poll()
{
    if (!openSharedMemory())
    {
        return;
    }

    Snapshot snapshot;
    if (!readSnapshot(snapshot))
    {
        return;
    }

    if (isActiveStatus(snapshot.gameStatus))
    {
        if (!m_wasActive)
        {
            // Empieza una partida nueva: recien aca se habilita un reporte.
            // Antes esto se reiniciaba en CADA lectura (4 veces por segundo),
            // asi que mientras estaba la pantalla de resultados se mandaba un
            // reporte tras otro y el historial se llenaba de duplicados.
            m_minStock.clear();
            m_matchProcessed = false;
        }
        m_wasActive = true;

        // Solo se toman valores mientras la partida corre de verdad. En la
        // pantalla de resultados el juego ya puso el daño en 0, y si se
        // guardara ese momento el historial mostraria "0%" siempre.
        const bool inPlay = (snapshot.gameStatus == 1 || snapshot.gameStatus == 2 ||
                             snapshot.gameStatus == 3);
        // Vidas minimas por jugador. Solo se acepta bajar de UNA en UNA (una
        // muerte real): al reiniciar, la memoria del juego puede leer 0 vidas
        // por un instante en todos los puertos, y ese 0 falso quedaba
        // guardado, dejando a los dos "sin vidas" el resto de la partida.
        // Si las vidas SUBEN, empezo otra partida (reinicio): se olvida lo
        // anterior. Tambien se mira en fin de partida / resultados para no
        // perder la ultima muerte si el juego cambia de estado entre lecturas.
        const bool trackStock = inPlay || snapshot.gameStatus == 5 || snapshot.gameStatus == 6;
        if (trackStock)
        {
            for (int i = 1; i <= NUM_PLAYERS; ++i)
            {
                const PlayerStats& p = snapshot.players[i - 1];
                if (!p.active)
                {
                    continue;
                }
                auto it = m_minStock.find(i);
                if (it == m_minStock.end())
                {
                    if (p.stock > 0)
                    {
                        m_minStock[i] = p.stock; // primera lectura valida
                    }
                    continue;
                }
                if (p.stock > it.value())
                {
                    // Vidas de vuelta arriba: otra partida.
                    m_minStock.clear();
                    m_matchProcessed = false;
                    m_haveLastActive = false;
                    for (int j = 1; j <= NUM_PLAYERS; ++j)
                    {
                        const PlayerStats& q = snapshot.players[j - 1];
                        if (q.active && q.stock > 0)
                        {
                            m_minStock[j] = q.stock;
                        }
                    }
                    break;
                }
                if (it.value() - p.stock == 1)
                {
                    it.value() = p.stock; // muerte real: una vida menos
                }
                // Cualquier otra cosa (salto de varias vidas) es una lectura
                // falsa y se ignora.
            }
        }
        if (inPlay)
        {
            m_lastActive = snapshot;
            m_haveLastActive = true;
        }

        // Reportar apenas el resultado ya esta decidido (alguien llego a 0
        // vidas: estados 5 "fin de partida" y 6 "mostrando resultado"), sin
        // esperar a que el juego vuelva al menu ni a que se cierre RMG-K.
        // Antes dependia de lo que hiciera el jugador: si dejaba el emulador
        // en la pantalla de resultados, o lo cerraba de golpe, su reporte no
        // salia nunca -- y como el servidor necesita los DOS, la partida se
        // perdia para ambos (paso de verdad en la partida 9712).
        if ((snapshot.gameStatus == 5 || snapshot.gameStatus == 6) &&
            !m_matchProcessed && m_haveLastActive)
        {
            // Se reporta con los ultimos valores EN JUEGO, no con los de la
            // pantalla de resultados (ahi el daño ya esta en cero).
            Snapshot decided = m_lastActive;
            decided.gameStatus = snapshot.gameStatus;
            for (int i = 1; i <= NUM_PLAYERS; ++i)
            {
                decided.players[i - 1].stock = m_minStock.value(i, decided.players[i - 1].stock);
            }
            // El criterio de "ya se decidio" depende del modo: en Team un
            // equipo entero tiene que quedarse sin nadie con vidas, no un
            // solo jugador (con 2 rivales todavia en pie no hay nada que
            // reportar aunque UN puerto ya se haya quedado en cero).
            OnlineBridge::ActiveMatchIdentity identity;
            const bool isTeam = m_bridge != nullptr && m_bridge->activeMatchIdentity(identity) &&
                identity.matchType == QStringLiteral("team");
            const bool decidedNow = isTeam
                ? (determineWinningTeam(decided) != 0)
                : (determineWinnerPort(decided) != 0);
            if (decidedNow)
            {
                m_matchProcessed = true;
                reportMatch(decided);
            }
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastPushMs >= 1000)
        {
            m_lastPushMs = now;
            pushLiveState(snapshot);
        }
        return;
    }

    m_wasActive = false;
    if (!m_haveLastActive || m_matchProcessed)
    {
        return;
    }

    releaseLiveState();

    // La partida acaba de terminar: se corrigen las vidas con el minimo
    // visto y se reporta una sola vez.
    Snapshot corrected = m_lastActive;
    for (int i = 1; i <= NUM_PLAYERS; ++i)
    {
        corrected.players[i - 1].stock = m_minStock.value(i, corrected.players[i - 1].stock);
    }

    m_matchProcessed = true;
    reportMatch(corrected);
}
