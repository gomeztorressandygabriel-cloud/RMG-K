/*
 * LiveStatsReporter - reporte de resultados de partida a la pagina.
 *
 * Es el equivalente en C++ de la parte de live_reader.py que le importa al
 * jugador final: leer la memoria compartida que publica el wrapper del
 * plugin de audio ("SmashRemixLiveReport"), detectar cuando termino una
 * partida, decidir quien gano, y mandarle ESE resultado a Supabase.
 *
 * Por que no se distribuye el script de Python en su lugar: haria falta
 * empaquetar Python y lanzar un proceso oculto en la PC de cada jugador --
 * justo lo que los antivirus marcan, y lo que ya habia causado el problema
 * de la consola apareciendo sola durante la partida. Adentro del emulador
 * eso no puede pasar.
 *
 * live_reader.py sigue existiendo y sirve igual para ver las stats en vivo
 * por consola mientras se desarrolla; las dos cosas pueden convivir (el
 * servidor ignora un reporte duplicado de la misma match_key).
 */
#ifndef LIVESTATSREPORTER_HPP
#define LIVESTATSREPORTER_HPP

#include <QObject>
#include <QString>
#include <QHash>

class QTimer;
class QNetworkAccessManager;
namespace UserInterface { class OnlineBridge; }
using OnlineBridge = UserInterface::OnlineBridge;

class LiveStatsReporter : public QObject
{
    Q_OBJECT

public:
    explicit LiveStatsReporter(OnlineBridge* bridge, QObject* parent = nullptr);
    ~LiveStatsReporter() override;

public:
    // Reporta una partida que quedo sin reportar y espera brevemente a que
    // salga. Se llama al cerrar RMG-K: el reporte normal se dispara cuando
    // el juego SALE de la pantalla de resultados, asi que quien cierra el
    // emulador ahi mismo nunca reportaba -- y el servidor necesita los dos
    // reportes para aplicar el resultado, con lo cual la partida entera se
    // perdia.
    void finalizePendingMatch();

private slots:
    void poll();

private:
    struct PlayerStats
    {
        bool     active = false;
        quint32  characterId = 0;
        quint32  damage = 0;
        quint32  stock = 0;
        quint32  totalDealt = 0;
        quint32  totalTaken = 0;
        quint32  currentCombo = 0;
        quint32  highestHitTaken = 0;
        quint32  maxComboHitsTaken = 0;
        quint32  maxComboDamageTaken = 0;
        quint32  maxComboDealt = 0;
        quint32  kos = 0;
        quint32  deaths = 0;
    };

    struct Snapshot
    {
        quint32 gameStatus = 0;
        quint32 stage = 0;
        double  elapsedSeconds = 0.0;
        PlayerStats players[4];
    };

    bool openSharedMemory();
    bool readSnapshot(Snapshot& out) const;
    // Puerto ganador (1-4), o 0 si no se puede decidir con certeza.
    int  determineWinnerPort(const Snapshot& snapshot) const;
    // Equipo ganador (1 o 2) para partidas de Team, o 0 si no se puede
    // decidir. Equipos por asiento: P1+P2 (el que arma el grupo e invita)
    // = equipo 1, P3+P4 (los dos invitados individualmente) = equipo 2.
    int  determineWinningTeam(const Snapshot& snapshot) const;
    void reportMatch(const Snapshot& snapshot);
    // Empuja el estado en vivo a la pagina. Solo uno de los dos clientes
    // termina mandando de verdad: el servidor le da el turno al primero que
    // escribe y rechaza al otro (ver web/sql/live_state_push.sql).
    void pushLiveState(const Snapshot& snapshot);
    void releaseLiveState();

    OnlineBridge*          m_bridge = nullptr;
    QTimer*                m_timer = nullptr;
    QNetworkAccessManager* m_network = nullptr;

    void*  m_mapping = nullptr;   // HANDLE
    const unsigned char* m_view = nullptr;

    // Las vidas SOLO bajan durante una partida, pero justo en el instante
    // del reset el contador puede mostrar un residual mas alto que el real.
    // Por eso se guarda el MINIMO visto durante toda la partida y se usa ese
    // para decidir el ganador (mismo criterio que live_reader.py).
    QHash<int, quint32> m_minStock;
    Snapshot m_lastActive;
    bool     m_haveLastActive = false;
    bool     m_wasActive = false;
    bool     m_matchProcessed = true;

    // Identifica a ESTE cliente frente al servidor para el turno de emision.
    QString m_liveToken;
    // El estado en vivo se manda ~1/s, no en cada tick de lectura: la pagina
    // no necesita mas y asi no se inunda la base de datos.
    qint64 m_lastPushMs = 0;
    bool   m_livePushActive = false;
};

#endif // LIVESTATSREPORTER_HPP
