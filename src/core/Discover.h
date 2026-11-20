#pragma once
#include "Track.h"
#include <QPair>
#include <QStringList>

namespace halo {

// The Discover view (SPEC §5, 2b): suggestions from the user's own listening
// history and library. Everything is computed locally; nothing is sent online.
struct DiscoverItem {
    QString label;          // album, artist or song name
    QString sublabel;       // artist, or a reason ("12 plays this month")
    TrackList tracks;       // what plays when the item is chosen
};

struct DiscoverShelf {
    QString title;
    QString subtitle;
    QVector<DiscoverItem> items;
};

namespace Discover {
    // history: (track id, played at) pairs; now: seconds since epoch (for tests).
    QVector<DiscoverShelf> shelves(const TrackList &library, const QVector<QPair<qint64, qint64>> &history, qint64 now);
}

} // namespace halo
