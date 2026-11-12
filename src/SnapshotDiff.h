#pragma once

#include <QVariantMap>

// What changed between two analyses of the same website: a semantic diff over the
// normalized summaries (port of the web dashboard's SnapshotDiff.php). Timings and other
// noise are not part of the summary, so every reported change is worth knowing.
namespace SnapshotDiff {

// { groups: [{ section, changes: [{ path, label, kind, before, after }] }],
//   unchangedSections: [name], count }
// kind is "added", "removed" or "changed"; before/after are display strings, empty when absent.
[[nodiscard]] QVariantMap diff(const QVariantMap &before, const QVariantMap &after);

} // namespace SnapshotDiff
