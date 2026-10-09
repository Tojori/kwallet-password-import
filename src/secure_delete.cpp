// SPDX-License-Identifier: LGPL-2.1-or-later
#include "secure_delete.h"

#include <QFile>

#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kwimport {

bool shredFile(const QString &path)
{
    const QByteArray p = QFile::encodeName(path);
    const int fd = ::open(p.constData(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    struct stat st {};
    bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
    if (ok) {
        static const char zeros[4096] = {};
        off_t left = st.st_size;
        while (ok && left > 0) {
            const ssize_t n = ::write(fd, zeros, size_t(std::min<off_t>(left, sizeof(zeros))));
            ok = n > 0;
            left -= n;
        }
        ok = ok && ::fsync(fd) == 0;
    }
    ::close(fd);
    return ok && ::unlink(p.constData()) == 0;
}

} // namespace kwimport
