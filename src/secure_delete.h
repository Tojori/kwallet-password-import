// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QString>

namespace kwimport {

// Overwrites a regular file with zeros, syncs and unlinks it. On copy-on-write
// file systems (btrfs, ZFS) and SSDs old blocks may survive; full-disk
// encryption is the real protection.
bool shredFile(const QString &path);

} // namespace kwimport
