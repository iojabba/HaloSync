#pragma once
#include "Track.h"

namespace halo {

struct DuplicateGroup {
    QString   label;     // "Artist – Title"
    TrackList tracks;
};

namespace Reports {
    // Same artist + title (case-insensitive) and length within 3 seconds.
    QVector<DuplicateGroup> duplicates(const TrackList &library);
    TrackList missingArt(const TrackList &library);
    // Missing title, artist, album or genre.
    TrackList missingTags(const TrackList &library);
}

} // namespace halo
