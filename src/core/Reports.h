#pragma once
#include "Track.h"

namespace halo {

struct DuplicateGroup {
    QString   label;     // "Artist – Title"
    TrackList tracks;
    bool      identical = false;   // byte-for-byte the same file content
    int       best = 0;            // index of the copy worth keeping
};

class Database;

namespace Reports {
    // Same artist + title (case-insensitive) and length within 3 seconds.
    QVector<DuplicateGroup> duplicates(const TrackList &library);
    TrackList missingArt(const TrackList &library);
    // Duplicate review (SPEC §5, 2b): identical files (content hash) and the same
    // song in several formats or bit rates, each with the best copy picked.
    // Hashes are cached in the database.
    QVector<DuplicateGroup> duplicateReview(const TrackList &library, Database *db);
    QString contentHash(const QString &path);      // size + SHA-1 of the first and last MiB
    int bestCopy(const TrackList &copies);         // lossless > bit rate > has art > plays
    bool isLossless(const Track &t);
    // Missing title, artist, album or genre.
    TrackList missingTags(const TrackList &library);
}

} // namespace halo
