/*
 * Logros, rachas y eventos del dia de un jugador, calculados a partir de su
 * historial de partidas. Solo depende de QtCore para poder probarse suelto.
 *
 * Los logros permanentes son los mismos que en la web (profile.js,
 * ACHIEVEMENTS): si se cambian aca, cambiarlos alla tambien.
 */
#ifndef ACHIEVEMENTLOGIC_HPP
#define ACHIEVEMENTLOGIC_HPP

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace Achievements
{

struct Match
{
    qint64 playedMs = 0;
    bool   win = false;
    int    characterId = -1;
    int    combo = 0;
};

struct Event
{
    QString title;
    QString text;
};

struct Stats
{
    int wins = 0;
    int games = 0;
    int bestStreak = 0;
    int currentStreak = 0;
    int bestCombo = 0;
    int winChars = 0;
};

struct Definition
{
    const char* id;
    const char* name;
    const char* desc;
    bool (*test)(const Stats&);
};

inline const QList<Definition>& definitions()
{
    static const QList<Definition> list = {
        {"first", "Primera victoria",  "Gana tu primera partida.",            [](const Stats& s) { return s.wins >= 1; }},
        {"w10",   "10 victorias",      "Llega a 10 victorias.",               [](const Stats& s) { return s.wins >= 10; }},
        {"w25",   "25 victorias",      "Llega a 25 victorias.",               [](const Stats& s) { return s.wins >= 25; }},
        {"w50",   "50 victorias",      "Llega a 50 victorias.",               [](const Stats& s) { return s.wins >= 50; }},
        {"w100",  "100 victorias",     "Llega a 100 victorias.",              [](const Stats& s) { return s.wins >= 100; }},
        {"g50",   "Veterano",          "Juega 50 partidas.",                  [](const Stats& s) { return s.games >= 50; }},
        {"s3",    "En racha",          "Gana 3 seguidas.",                    [](const Stats& s) { return s.bestStreak >= 3; }},
        {"s5",    "Imparable",         "Gana 5 seguidas.",                    [](const Stats& s) { return s.bestStreak >= 5; }},
        {"s10",   "Leyenda",           "Gana 10 seguidas.",                   [](const Stats& s) { return s.bestStreak >= 10; }},
        {"combo", "Combo 10+",         "Conecta un combo de 10 golpes o mas.", [](const Stats& s) { return s.bestCombo >= 10 && s.bestCombo < 1000; }},
        {"chars", "Polivalente",       "Gana con 5 personajes distintos.",    [](const Stats& s) { return s.winChars >= 5; }},
    };
    return list;
}

struct Result
{
    QList<Event> events;      // novedades para avisar
    QStringList unlocked;     // ids de todos los logros permanentes cumplidos
    qint64 lastMs = 0;        // partida mas reciente vista
    Stats stats;
};

inline bool isStreakMilestone(int streak)
{
    return streak == 3 || streak == 5 || streak == 7 || streak == 10 || (streak > 10 && streak % 5 == 0);
}

// seed = true: primera vez que se mira a este jugador -> se guarda el estado
// pero no se avisa de nada que ya habia pasado.
// lastMs: ultima partida ya procesada (solo las posteriores generan avisos).
inline Result evaluate(QList<Match> matches, const QStringList& previouslyUnlocked, qint64 lastMs, bool seed)
{
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) { return a.playedMs < b.playedMs; });

    Result result;
    Stats& st = result.stats;
    QSet<int> winChars;
    QHash<QDate, int> dayGames;
    QSet<QDate> winDays;
    int streakBaseBest = 0; // mejor racha ANTES de empezar la racha actual

    for (const Match& m : matches)
    {
        const bool isNew = !seed && m.playedMs > lastMs;
        const QDate day = QDateTime::fromMSecsSinceEpoch(m.playedMs).toLocalTime().date();

        ++st.games;
        const int gamesToday = ++dayGames[day];

        if (m.win)
        {
            ++st.wins;
            ++st.currentStreak;
            if (m.characterId >= 0)
                winChars.insert(m.characterId);

            if (!winDays.contains(day))
            {
                winDays.insert(day);
                if (isNew)
                    result.events.append({QStringLiteral("Primera victoria del dia"),
                                          QStringLiteral("Ya sumaste tu primera victoria de hoy.")});
            }

            if (st.currentStreak == 1)
                streakBaseBest = st.bestStreak;
            const int previousBest = streakBaseBest;
            if (st.currentStreak > st.bestStreak)
                st.bestStreak = st.currentStreak;

            if (isNew && st.currentStreak >= 3)
            {
                // Solo al SUPERAR el record (no en cada victoria de una racha
                // larga); el resto de la racha avisa en los hitos.
                if (st.currentStreak == previousBest + 1 && previousBest >= 3)
                    result.events.append({QStringLiteral("Nueva mejor racha!"),
                                          QStringLiteral("%1 victorias seguidas, tu mejor racha hasta ahora.").arg(st.currentStreak)});
                else if (isStreakMilestone(st.currentStreak))
                    result.events.append({QStringLiteral("Racha de %1").arg(st.currentStreak),
                                          QStringLiteral("%1 victorias seguidas. Segui asi!").arg(st.currentStreak)});
            }
        }
        else
        {
            st.currentStreak = 0;
        }

        st.bestCombo = qMax(st.bestCombo, m.combo);

        if (isNew && (gamesToday == 5 || gamesToday == 10 || gamesToday == 20))
            result.events.append({QStringLiteral("%1 partidas hoy").arg(gamesToday),
                                  QStringLiteral("Hoy ya jugaste %1 partidas.").arg(gamesToday)});

        result.lastMs = qMax(result.lastMs, m.playedMs);
    }
    st.winChars = winChars.size();

    for (const Definition& d : definitions())
    {
        if (!d.test(st))
            continue;
        result.unlocked.append(QString::fromLatin1(d.id));
        if (!seed && !previouslyUnlocked.contains(QString::fromLatin1(d.id)))
            result.events.append({QStringLiteral("Logro desbloqueado!"),
                                  QStringLiteral("%1: %2").arg(QString::fromLatin1(d.name), QString::fromLatin1(d.desc))});
    }
    return result;
}

} // namespace Achievements

#endif // ACHIEVEMENTLOGIC_HPP
