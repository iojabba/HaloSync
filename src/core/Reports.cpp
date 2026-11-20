#include "Reports.h"
#include "Database.h"
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QSet>
#include <algorithm>

namespace halo {

QVector<DuplicateGroup> Reports::duplicates(const TrackList &library)
{
    QHash<QString, TrackList> byKey;
    for (const Track &t : library) {
        if (t.title.isEmpty()) continue;
        byKey[t.displayArtist().toLower().simplified() + QChar(0x1f) + t.title.toLower().simplified()].push_back(t);
    }
    QVector<DuplicateGroup> out;
    for (auto it = byKey.begin(); it != byKey.end(); ++it) {
        TrackList list = it.value();
        if (list.size() < 2) continue;
        std::sort(list.begin(), list.end(), [](const Track &a, const Track &b) { return a.durationMs < b.durationMs; });
        // Split into clusters of similar length (different edits aren't duplicates).
        TrackList cluster{list.front()};
        auto flush = [&] {
            if (cluster.size() > 1)
                out.push_back({cluster.front().displayArtist() + QStringLiteral(" – ") + cluster.front().displayTitle(), cluster});
        };
        for (int i = 1; i < list.size(); ++i) {
            if (list[i].durationMs - cluster.back().durationMs <= 3000) cluster.push_back(list[i]);
            else { flush(); cluster = {list[i]}; }
        }
        flush();
    }
    std::sort(out.begin(), out.end(), [](const DuplicateGroup &a, const DuplicateGroup &b) {
        return a.label.compare(b.label, Qt::CaseInsensitive) < 0; });
    return out;
}

TrackList Reports::missingArt(const TrackList &library)
{
    TrackList out;
    for (const Track &t : library) if (!t.hasArt) out.push_back(t);
    return out;
}

TrackList Reports::missingTags(const TrackList &library)
{
    TrackList out;
    for (const Track &t : library)
        if (t.title.isEmpty() || (t.artist.isEmpty() && t.albumArtist.isEmpty()) || t.album.isEmpty() || t.genre.isEmpty())
            out.push_back(t);
    return out;
}

bool Reports::isLossless(const Track &t)
{
    static const QSet<QString> lossless = {"flac", "wav", "aif", "aiff", "ape", "wv"};
    return lossless.contains(t.format);
}

int Reports::bestCopy(const TrackList &copies)
{
    auto score = [](const Track &t) {
        return std::make_tuple(isLossless(t) ? 1 : 0, t.bitrateKbps, t.hasArt ? 1 : 0, t.playCount, t.rating, -t.dateAdded);
    };
    int best = 0;
    for (int i = 1; i < copies.size(); ++i) if (score(copies[i]) > score(copies[best])) best = i;
    return best;
}

QString Reports::contentHash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    constexpr qint64 chunk = 1 << 20;
    QCryptographicHash h(QCryptographicHash::Sha1);
    const qint64 size = f.size();
    h.addData(QByteArray::number(size));
    h.addData(f.read(chunk));
    if (size > 2 * chunk) { f.seek(size - chunk); h.addData(f.read(chunk)); }
    else h.addData(f.readAll());
    return QString::fromLatin1(h.result().toHex());
}

QVector<DuplicateGroup> Reports::duplicateReview(const TrackList &library, Database *db)
{
    QVector<DuplicateGroup> out;
    QSet<qint64> grouped;
    // 1. Identical files: only files with an equal size need hashing.
    QHash<qint64, TrackList> bySize;
    for (const Track &t : library) if (t.sizeBytes > 0) bySize[t.sizeBytes] << t;
    for (const TrackList &same : bySize) {
        if (same.size() < 2) continue;
        QHash<QString, TrackList> byHash;
        for (const Track &t : same) {
            QString h = db ? db->cachedHash(t.path, t.sizeBytes, t.mtime) : QString();
            if (h.isEmpty()) {
                h = contentHash(t.path);
                if (db && !h.isEmpty()) db->storeHash(t.path, t.sizeBytes, t.mtime, h);
            }
            if (!h.isEmpty()) byHash[h] << t;
        }
        for (const TrackList &g : byHash) {
            if (g.size() < 2) continue;
            DuplicateGroup d{g.front().displayArtist() + QStringLiteral(" – ") + g.front().displayTitle(), g, true, bestCopy(g)};
            out << d;
            for (const Track &t : g) grouped.insert(t.id);
        }
    }
    // 2. Same song (artist, title, similar length) in other files: other formats, bit rates or rips.
    for (DuplicateGroup g : duplicates(library)) {
        TrackList rest;
        for (const Track &t : g.tracks) if (!grouped.contains(t.id)) rest << t;
        if (rest.size() < 2) continue;      // the copies were all identical files, already listed
        g.tracks = rest;
        g.identical = false;
        g.best = bestCopy(rest);
        out << g;
    }
    std::sort(out.begin(), out.end(), [](const DuplicateGroup &a, const DuplicateGroup &b) {
        return a.label.compare(b.label, Qt::CaseInsensitive) < 0; });
    return out;
}

} // namespace halo
