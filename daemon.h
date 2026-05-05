#ifndef DAEMON_H
#define DAEMON_H

/*
 * Przekształca bieżący proces w demona zgodnie ze standardem POSIX:
 *
 *   1. fork()   – rodzic kończy działanie; shell odzyskuje kontrolę
 *   2. setsid() – nowa sesja; odłączenie od terminala sterującego
 *   3. fork()   – gwarancja, że demon nigdy nie stanie się liderem sesji
 *   4. umask(0) – brak ukrytych ograniczeń praw dostępu do nowych plików
 *   5. chdir("/")       – zwolnienie bieżącego katalogu (może być na innym fs)
 *   6. stdin/stdout/stderr → /dev/null  – brak przypadkowego I/O na terminal
 *
 * W przypadku błędu funkcja wypisuje komunikat przez perror() i kończy
 * proces z EXIT_FAILURE (jest wywoływana przed openlog(), więc syslog
 * jeszcze nie działa).
 */
void daemonize(void);

#endif /* DAEMON_H */
