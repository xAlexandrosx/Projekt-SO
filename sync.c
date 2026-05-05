#include "sync.h"
#include "copy.h"

#include <sys/stat.h>
#include <dirent.h>     /* opendir(), readdir(), closedir() */
#include <unistd.h>     /* unlink(), rmdir() */
#include <string.h>
#include <limits.h>     /* PATH_MAX */
#include <syslog.h>
#include <errno.h>
#include <stdio.h>      /* snprintf() */

/* ── usuwanie rekurencyjne (tylko do użytku wewnętrznego) ───────────────── */

/*
 * Usuwa path i całą jego zawartość (odpowiednik `rm -rf`).
 * Używany gdy katalog istnieje w target, ale znikł ze source.
 */
static void remove_recursive(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;

    /* Pliki i dowiązania symboliczne usuwamy bezpośrednio */
    if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
        if (unlink(path) == 0)
            syslog(LOG_INFO, "Usunieto plik: %s", path);
        else
            syslog(LOG_ERR, "Blad usuwania pliku %s: %s", path, strerror(errno));
        return;
    }

    if (!S_ISDIR(st.st_mode)) return;  /* inne typy – pomijamy */

    DIR *dir = opendir(path);
    if (!dir) {
        syslog(LOG_ERR, "Nie mozna otworzyc katalogu %s: %s", path, strerror(errno));
        return;
    }

    struct dirent *entry;
    char child[PATH_MAX];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(child, PATH_MAX, "%s/%s", path, entry->d_name);
        remove_recursive(child);   /* rekurencja w głąb */
    }
    closedir(dir);

    /* Katalog jest teraz pusty – można go usunąć */
    if (rmdir(path) == 0)
        syslog(LOG_INFO, "Usunieto katalog: %s", path);
    else
        syslog(LOG_ERR, "Blad usuwania katalogu %s: %s", path, strerror(errno));
}

/* ── publiczny interfejs modułu ─────────────────────────────────────────── */

void synchronize_folders(const char *source, const char *target,
                         bool recursive, off_t threshold) {
    struct stat st_src, st_dst;
    char src_path[PATH_MAX], dst_path[PATH_MAX];

    /* ── Przebieg 1: źródło → cel ───────────────────────────────────────── */

    DIR *dir = opendir(source);
    if (!dir) {
        syslog(LOG_ERR, "Nie mozna otworzyc katalogu zrodlowego %s: %s",
               source, strerror(errno));
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(src_path, PATH_MAX, "%s/%s", source, entry->d_name);
        snprintf(dst_path, PATH_MAX, "%s/%s", target, entry->d_name);

        if (lstat(src_path, &st_src) != 0) continue;

        if (S_ISREG(st_src.st_mode)) {
            /*
             * Zwykły plik: kopiuj gdy brak w target lub źródło jest nowsze.
             * Porównanie mtime działa poprawnie tylko dlatego, że copy_file()
             * przywraca czas modyfikacji z pliku źródłowego po skopiowaniu.
             */
            if (lstat(dst_path, &st_dst) != 0) {
                copy_file(src_path, dst_path, &st_src, threshold);
            } else if (st_src.st_mtime > st_dst.st_mtime) {
                copy_file(src_path, dst_path, &st_src, threshold);
            }

        } else if (recursive && S_ISDIR(st_src.st_mode)) {
            /* Katalog (tylko przy fladze -R): utwórz jeśli brak, potem rekurencja */
            if (lstat(dst_path, &st_dst) != 0) {
                if (mkdir(dst_path, st_src.st_mode & 0777) == 0)
                    syslog(LOG_INFO, "Utworzono katalog: %s", dst_path);
                else {
                    syslog(LOG_ERR, "Blad tworzenia katalogu %s: %s",
                           dst_path, strerror(errno));
                    continue;
                }
            }
            synchronize_folders(src_path, dst_path, recursive, threshold);
        }
        /* Dowiązania symboliczne, urządzenia itp. – ignorujemy */
    }
    closedir(dir);

    /* ── Przebieg 2: usuwanie z celu tego, czego brak w źródle ─────────── */

    dir = opendir(target);
    if (!dir) {
        syslog(LOG_ERR, "Nie mozna otworzyc katalogu docelowego %s: %s",
               target, strerror(errno));
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(src_path, PATH_MAX, "%s/%s", source, entry->d_name);
        snprintf(dst_path, PATH_MAX, "%s/%s", target, entry->d_name);

        if (lstat(dst_path, &st_dst) != 0) continue;

        /* Istnieje w target, ale nie w source → usuń */
        if (lstat(src_path, &st_src) != 0) {
            if (S_ISREG(st_dst.st_mode)) {
                if (unlink(dst_path) == 0)
                    syslog(LOG_INFO, "Usunieto plik: %s", dst_path);
                else
                    syslog(LOG_ERR, "Blad usuwania pliku %s: %s",
                           dst_path, strerror(errno));
            } else if (recursive && S_ISDIR(st_dst.st_mode)) {
                remove_recursive(dst_path);
            }
        }
    }
    closedir(dir);
}
