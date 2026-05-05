#include "copy.h"

#include <fcntl.h>      /* open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC */
#include <unistd.h>     /* read(), write(), close() */
#include <sys/mman.h>   /* mmap(), munmap(), MAP_FAILED */
#include <utime.h>      /* utime(), struct utimbuf */
#include <syslog.h>
#include <string.h>     /* strerror() */
#include <errno.h>

/* ── metody kopiowania (tylko do użytku wewnętrznego tego modułu) ─────── */

/*
 * Kopiuje zawartość src_fd do dst_fd przy użyciu bufora 4 KB.
 * Wewnętrzna pętla zapisu gwarantuje, że write() nie pominie żadnych bajtów
 * (np. na sieciowych systemach plików write() może zapisać mniej niż żądano).
 */
static void copy_rw(int src_fd, int dst_fd, const char *src_path) {
    char buf[4096];
    ssize_t n;

    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(dst_fd, buf + written, (size_t)(n - written));
            if (w < 0) {
                syslog(LOG_ERR, "Blad zapisu (read/write): %s", strerror(errno));
                return;
            }
            written += w;
        }
    }

    if (n < 0)
        syslog(LOG_ERR, "Blad odczytu (read/write) z %s: %s",
               src_path, strerror(errno));
}

/*
 * Kopiuje zawartość src_fd do dst_fd mapując cały plik w pamięci (mmap).
 * Przy błędzie mmap przełącza się automatycznie na copy_rw().
 */
static void copy_mmap(int src_fd, int dst_fd, off_t size, const char *src_path) {
    void *mapped = mmap(NULL, (size_t)size, PROT_READ, MAP_SHARED, src_fd, 0);

    if (mapped == MAP_FAILED) {
        syslog(LOG_WARNING,
               "mmap nie powiodlo sie dla %s (%s) – przelaczam na read/write",
               src_path, strerror(errno));
        copy_rw(src_fd, dst_fd, src_path);
        return;
    }

    const char *ptr = (const char *)mapped;
    size_t remaining = (size_t)size;

    while (remaining > 0) {
        ssize_t w = write(dst_fd, ptr, remaining);
        if (w < 0) {
            if (errno == EINTR) continue;   /* przerwany przez sygnał – próbuj dalej */
            syslog(LOG_ERR, "Blad zapisu (mmap) dla %s: %s",
                   src_path, strerror(errno));
            break;
        }
        ptr       += w;
        remaining -= (size_t)w;
    }

    munmap(mapped, (size_t)size);
}

/* ── publiczny interfejs modułu ─────────────────────────────────────────── */

void copy_file(const char *src_path, const char *dst_path,
               const struct stat *st, off_t threshold) {
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        syslog(LOG_ERR, "Nie mozna otworzyc zrodla %s: %s",
               src_path, strerror(errno));
        return;
    }

    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, st->st_mode & 0777);
    if (dst_fd < 0) {
        syslog(LOG_ERR, "Nie mozna otworzyc/utworzyc celu %s: %s",
               dst_path, strerror(errno));
        close(src_fd);
        return;
    }

    if (st->st_size >= threshold) {
        syslog(LOG_INFO, "Kopiowanie (mmap)       [%7ld B]: %s -> %s",
               (long)st->st_size, src_path, dst_path);
        copy_mmap(src_fd, dst_fd, st->st_size, src_path);
    } else {
        syslog(LOG_INFO, "Kopiowanie (read/write) [%7ld B]: %s -> %s",
               (long)st->st_size, src_path, dst_path);
        copy_rw(src_fd, dst_fd, src_path);
    }

    close(src_fd);
    close(dst_fd);

    /*
     * Przywracamy datę modyfikacji z pliku źródłowego.
     * Dzięki temu przy kolejnym obudzeniu demona porównanie mtime
     * nie wymusi ponownego kopiowania niezmienionego pliku.
     */
    struct utimbuf times = {
        .actime  = st->st_atime,
        .modtime = st->st_mtime
    };
    if (utime(dst_path, &times) < 0)
        syslog(LOG_WARNING, "Nie mozna ustawic czasu modyfikacji %s: %s",
               dst_path, strerror(errno));
}
