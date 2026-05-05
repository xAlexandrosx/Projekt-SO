#include "daemon.h"

#include <unistd.h>     /* fork(), setsid(), chdir(), dup2() */
#include <sys/stat.h>   /* umask() */
#include <fcntl.h>      /* open(), O_RDWR */
#include <stdlib.h>     /* exit(), EXIT_FAILURE, EXIT_SUCCESS */
#include <stdio.h>      /* perror() */

void daemonize(void) {
    /* ── Krok 1: pierwsze rozwidlenie ───────────────────────────────────── */
    /*
     * Rodzic kończy działanie – shell widzi zakończenie procesu i zwraca
     * prompt użytkownikowi. Dziecko kontynuuje jako proces bez terminala.
     */
    pid_t pid = fork();
    if (pid < 0) { perror("fork (1)"); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS);    /* rodzic kończy */

    /* ── Krok 2: nowa sesja ─────────────────────────────────────────────── */
    /*
     * setsid() tworzy nową sesję i czyni bieżący proces jej liderem.
     * Nowa sesja nie ma przypisanego terminala sterującego – demon
     * nie otrzyma już SIGHUP gdy terminal zostanie zamknięty.
     */
    if (setsid() < 0) { perror("setsid"); exit(EXIT_FAILURE); }

    /* ── Krok 3: drugie rozwidlenie ─────────────────────────────────────── */
    /*
     * Lider sesji może w przyszłości ponownie uzyskać terminal sterujący
     * (przez open() na urządzeniu /dev/ttyX). Drugie fork() sprawia,
     * że demon NIE jest liderem sesji i ten problem znika na stałe.
     */
    pid = fork();
    if (pid < 0) { perror("fork (2)"); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS);    /* pośredni rodzic kończy */

    /* ── Krok 4: maska uprawnień ─────────────────────────────────────────── */
    /*
     * umask(0) wyłącza domyślne odejmowanie bitów uprawnień przy tworzeniu
     * plików. Demon sam jawnie ustawia prawa w open() / mkdir() – nie chcemy
     * żadnych ukrytych modyfikacji.
     */
    umask(0);

    /* ── Krok 5: katalog roboczy ─────────────────────────────────────────── */
    /*
     * Jeśli demon pozostałby w katalogu startowym, system plików zawierający
     * ten katalog nie mógłby zostać odmontowany (busy). Przejście do "/" jest
     * bezpieczne – katalog główny nigdy nie jest odmontowywany.
     */
    if (chdir("/") < 0) { perror("chdir"); exit(EXIT_FAILURE); }

    /* ── Krok 6: odłączenie standardowych deskryptorów ─────────────────── */
    /*
     * Zamknięcie (właściwie: przekierowanie na /dev/null) stdin/stdout/stderr
     * zapobiega przypadkowemu wypisaniu czegoś na terminal lub czytaniu
     * danych od użytkownika. /dev/null pochłania wszystko bez błędów.
     */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO)
            close(devnull);     /* nie duplikujemy deskryptora 0-2 */
    }
}
