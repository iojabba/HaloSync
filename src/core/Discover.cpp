#include "Discover.h"
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <algorithm>

namespace halo {

namespace {
constexpr qint64 Day = 24 * 3600;
QString albumKey(const Track &t) { return t.displayAlbumArtist().toLower() + '\x1f' + t.displayAlbum().toLower(); }

TrackList albumOrder(TrackList list)
{
    std::stable_sort(list.begin(), list.end(), [](const Track &a, const Track &b) {
        if (a.discNo != b.discNo) return a.discNo < b.discNo;
        return a.trackNo < b.trackNo;
    });
    return list;
}
} // namespace

QVector<DiscoverShelf> Discover::shelves(const TrackList &library, const QVector<QPair<qint64, qint64>> &history, qint64 now)
{
    QVector<DiscoverShelf> out;
    QHash<qint64, const Track *> byId;
    QHash<QString, TrackList> albums, artists;
    for (const Track &t : library) {
        byId.insert(t.id, &t);
        if (!t.album.isEmpty()) albums[albumKey(t)] << t;
        artists[t.displayAlbumArtist()] << t;
    }

    // Plays per album and per artist in the last 30 days.
    QHash<QString, int> albumPlays, artistPlays;
    QHash<QString, int> artistPlaysAll;
    for (const auto &h : history) {
        const Track *t = byId.value(h.first);
        if (!t) continue;
        artistPlaysAll[t->displayAlbumArtist()]++;
        if (h.second < now - 30 * Day) continue;
        if (!t->album.isEmpty()) albumPlays[albumKey(*t)]++;
        artistPlays[t->displayAlbumArtist()]++;
    }

    // 1. Your month: most played albums.
    {
        QVector<QPair<int, QString>> ranked;
        for (auto it = albumPlays.begin(); it != albumPlays.end(); ++it) ranked.push_back({it.value(), it.key()});
        std::sort(ranked.begin(), ranked.end(), [](auto &a, auto &b) { return a.first > b.first; });
        DiscoverShelf s{QStringLiteral("your month"), QStringLiteral("the albums you played most in the last 30 days"), {}};
        for (const auto &r : ranked.mid(0, 12)) {
            const TrackList tl = albumOrder(albums.value(r.second));
            if (tl.isEmpty()) continue;
            s.items.push_back({tl.front().displayAlbum(), QStringLiteral("%1 · %2 plays").arg(tl.front().displayAlbumArtist()).arg(r.first), tl});
        }
        if (!s.items.isEmpty()) out << s;
    }

    // 2. Back in rotation: songs you loved or played a lot, but not for two months.
    {
        TrackList list;
        for (const Track &t : library)
            if ((t.rating > 0 || t.playCount >= 5) && t.lastPlayed > 0 && t.lastPlayed < now - 60 * Day) list << t;
        std::sort(list.begin(), list.end(), [](const Track &a, const Track &b) {
            return std::make_pair(a.rating, a.playCount) > std::make_pair(b.rating, b.playCount); });
        DiscoverShelf s{QStringLiteral("back in rotation"), QStringLiteral("favourites you haven't played for a while"), {}};
        for (const Track &t : list.mid(0, 20))
            s.items.push_back({t.displayTitle(), QStringLiteral("%1 · last played %2").arg(t.displayArtist(),
                                QDateTime::fromSecsSinceEpoch(t.lastPlayed).toString("MMM yyyy")), {t}});
        if (!s.items.isEmpty()) out << s;
    }

    // 3. Never played: albums added over two weeks ago that you haven't played a song of.
    {
        QVector<QPair<qint64, QString>> list;
        for (auto it = albums.begin(); it != albums.end(); ++it) {
            bool played = false;
            qint64 added = now;
            for (const Track &t : it.value()) { played |= t.playCount > 0; added = std::min(added, t.dateAdded); }
            if (!played && added < now - 14 * Day) list.push_back({added, it.key()});
        }
        std::sort(list.begin(), list.end(), [](auto &a, auto &b) { return a.first > b.first; });   // newest first
        DiscoverShelf s{QStringLiteral("you haven't played"), QStringLiteral("albums in your collection waiting for a first listen"), {}};
        for (const auto &l : list.mid(0, 16)) {
            const TrackList tl = albumOrder(albums.value(l.second));
            s.items.push_back({tl.front().displayAlbum(), tl.front().displayAlbumArtist(), tl});
        }
        if (!s.items.isEmpty()) out << s;
    }

    // 4. More like your favourites: artists in your favourite artists' genres that you rarely play.
    {
        QVector<QPair<int, QString>> top;
        for (auto it = artistPlaysAll.begin(); it != artistPlaysAll.end(); ++it) top.push_back({it.value(), it.key()});
        std::sort(top.begin(), top.end(), [](auto &a, auto &b) { return a.first > b.first; });
        QHash<QString, int> genreWeight;
        QSet<QString> favourites;
        for (const auto &a : top.mid(0, 8)) {
            favourites.insert(a.second);
            for (const Track &t : artists.value(a.second)) if (!t.genre.isEmpty()) genreWeight[t.genre.toLower()]++;
        }
        QVector<QPair<double, QString>> picks;
        for (auto it = artists.begin(); it != artists.end(); ++it) {
            if (favourites.contains(it.key()) || it.key() == QLatin1String("Unknown Artist")) continue;
            int match = 0;
            for (const Track &t : it.value()) match += genreWeight.value(t.genre.toLower(), 0) > 0 ? 1 : 0;
            if (match == 0) continue;
            const double share = double(match) / double(it.value().size());
            const double rarely = 1.0 / (1.0 + artistPlaysAll.value(it.key(), 0));
            picks.push_back({share * rarely, it.key()});
        }
        std::sort(picks.begin(), picks.end(), [](auto &a, auto &b) { return a.first > b.first; });
        DiscoverShelf s{QStringLiteral("more like your favourites"), QStringLiteral("artists from the genres you play most"), {}};
        for (const auto &p : picks.mid(0, 12)) {
            TrackList tl = artists.value(p.second);
            QString genre;
            for (const Track &t : tl) if (!t.genre.isEmpty()) { genre = t.genre; break; }
            std::sort(tl.begin(), tl.end(), [](const Track &a, const Track &b) {
                if (int c = a.displayAlbum().compare(b.displayAlbum(), Qt::CaseInsensitive)) return c < 0;
                return std::make_pair(a.discNo, a.trackNo) < std::make_pair(b.discNo, b.trackNo); });
            s.items.push_back({p.second, genre, tl});
        }
        if (!s.items.isEmpty()) out << s;
    }

    // 5. On this day: what you played on today's date in earlier years.
    {
        const QDate today = QDateTime::fromSecsSinceEpoch(now).date();
        QHash<qint64, int> count;
        QHash<qint64, int> year;
        for (const auto &h : history) {
            const QDate d = QDateTime::fromSecsSinceEpoch(h.second).date();
            if (d.year() < today.year() && d.month() == today.month() && d.day() == today.day()) {
                count[h.first]++;
                year[h.first] = d.year();
            }
        }
        DiscoverShelf s{QStringLiteral("on this day"), QStringLiteral("what you were listening to on %1 in earlier years")
                            .arg(today.toString("d MMMM")), {}};
        for (auto it = count.begin(); it != count.end() && s.items.size() < 20; ++it)
            if (const Track *t = byId.value(it.key()))
                s.items.push_back({t->displayTitle(), QStringLiteral("%1 · %2").arg(t->displayArtist()).arg(year.value(it.key())), {*t}});
        if (!s.items.isEmpty()) out << s;
    }
    return out;
}

} // namespace halo
