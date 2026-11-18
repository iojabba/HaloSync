#include "Reports.h"
#include <QHash>
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

} // namespace halo
