#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>     /* getopt(), sleep() */
#include <sys/stat.h>
#include <syslog.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>   /* PATH_MAX */

#include "daemon.h"
#include "signals.h"
#include "sync.h"

/* ── stałe domyślne ─────────────────────────────────────────────────────── */

#define DEFAULT_SLEEP_SECS  300             /* 5 minut */
#define DEFAULT_THRESHOLD   (100 * 1024)    /* 100 KB  */

/* ── pomoc dla użytkownika ──────────────────────────────────────────────── */

static void usage(const char *prog) {
    fprintf(stderr,
        "Uzycie: %s [-R] [-t prog_bajtow] <zrodlo> <cel> [sekundy]\n"
        "\n"
        "  <zrodlo>        katalog zrodlowy (wzorzec)\n"
        "  <cel>           katalog docelowy (kopia)\n"
        "  [sekundy]       interwał synchronizacji (domyslnie: %d)\n"
        "\n"
        "  -R              rekurencyjna synchronizacja podkatalogow\n"
        "  -t <bajty>      prog rozmiaru pliku dla mmap (domyslnie: %d)\n"
        "                  pliki >= progu: mmap/write\n"
        "                  pliki  < progu: read/write\n"
        "\n"
        "Sygnaly:\n"
        "  SIGUSR1         natychmiastowe obudzenie i synchronizacja\n"
        "  SIGTERM         zatrzymanie demona\n",
        prog, DEFAULT_SLEEP_SECS, DEFAULT_THRESHOLD);
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    bool  recursive = false;
    off_t threshold = DEFAULT_THRESHOLD;

    /* ── Parsowanie opcji ────────────────────────────────────────────────── */

    int opt;
    while ((opt = getopt(argc, argv, "Rt:h")) != -1) {
        switch (opt) {
            case 'R':
                recursive = true;
                break;
            case 't': {
                long v = atol(optarg);
                if (v <= 0) {
                    fprintf(stderr, "Blad: prog (-t) musi byc liczba dodatnia.\n");
                    return EXIT_FAILURE;
                }
                threshold = (off_t)v;
                break;
            }
            case 'h':
                usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    /* ── Argumenty pozycyjne ─────────────────────────────────────────────── */

    if (optind + 1 >= argc) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    char abs_source[PATH_MAX], abs_target[PATH_MAX];

    if (!realpath(argv[optind], abs_source)) {
        perror("realpath (source)");
        return EXIT_FAILURE;
    }
    if (!realpath(argv[optind + 1], abs_target)) {
        perror("realpath (target)");
        return EXIT_FAILURE;
    }

    char* source = abs_source;
    char* target = abs_target;


    int   sleep_sec = (optind + 2 < argc)
                        ? atoi(argv[optind + 2])
                        : DEFAULT_SLEEP_SECS;

    if (sleep_sec <= 0) {
        fprintf(stderr, "Blad: czas spania musi byc liczba dodatnia.\n");
        return EXIT_FAILURE;
    }

    /* ── Weryfikacja ścieżek (przed daemonize – błędy trafiają na terminal) */

    struct stat st;
    if (stat(source, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Blad: '%s' nie jest katalogiem lub nie istnieje.\n", source);
        return EXIT_FAILURE;
    }
    if (stat(target, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Blad: '%s' nie jest katalogiem lub nie istnieje.\n", target);
        return EXIT_FAILURE;
    }

    /* ── Uruchomienie demona ─────────────────────────────────────────────── */

    daemonize();        /* po tym wywołaniu jesteśmy w tle, bez terminala */
    setup_signals();    /* instalujemy handler SIGUSR1 */

    openlog("FolderSyncDaemon", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    syslog(LOG_INFO,
           "Demon uruchomiony. Zrodlo: %s, Cel: %s, Czas: %ds, "
           "Rekurencja: %s, Prog mmap: %ld B",
           source, target, sleep_sec,
           recursive ? "tak" : "nie",
           (long)threshold);

    /* ── Główna pętla demona ─────────────────────────────────────────────── */

    while (1) {
        syslog(LOG_INFO, "Demon wybudzony – rozpoczynam synchronizacje.");

        synchronize_folders(source, target, recursive, threshold);

        syslog(LOG_INFO,
               "Synchronizacja zakonczona. Demon zasypia na %d sekund.", sleep_sec);

        /*
         * sleep() jest przerywany przez sygnał (SIGUSR1) i zwraca
         * liczbę pozostałych sekund. Pętla ponawia sen, jeśli przerwa
         * nie była spowodowana naszym sygnałem.
         */
        int remaining = sleep_sec;
        while (remaining > 0 && !wakeup_requested)
            remaining = (int)sleep((unsigned int)remaining);

        if (wakeup_requested) {
            syslog(LOG_INFO, "Demon wybudzony sygnalem SIGUSR1.");
            wakeup_requested = 0;
        }
    }

    closelog();     /* nieosiągalne, ale poprawne */
    return EXIT_SUCCESS;
}
