#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>

// przykładowe użycie programu
// gcc program.c -o program
// ./program "$(pwd)/zrodlo" "$(pwd)/cel" 10 

volatile sig_atomic_t wakeup_requested = 0;

void sigusr1_handler(int sig) {
    wakeup_requested = 1;
}

// funkcja do kopiowania plików na api linuxowym
void copy_file(const char *src_path, const char *dst_path, struct stat *st) {
    int src_fd = open(src_path, O_RDONLY);
    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, st->st_mode);

    if (src_fd < 0 || dst_fd < 0) {
        syslog(LOG_ERR, "Blad podczas otwierania plikow do kopiowania: %s", strerror(errno));
        return;
    }

    char buffer[4096]; // 4kb
    ssize_t bytes;
    while ((bytes = read(src_fd, buffer, sizeof(buffer))) > 0) {
        write(dst_fd, buffer, bytes);
    }

    close(src_fd);
    close(dst_fd);

    struct utimbuf new_times;
    new_times.actime = st->st_atime;
    new_times.modtime = st->st_mtime;
    utime(dst_path, &new_times);

    syslog(LOG_INFO, "Skopiowano plik: %s", src_path);
}


void synchronize_folders(const char *source, const char *target) {
    DIR *dir;
    struct dirent *entry;
    struct stat st_src, st_dst;
    char src_path[PATH_MAX], dst_path[PATH_MAX];

    dir = opendir(source);
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        snprintf(src_path, PATH_MAX, "%s/%s", source, entry->d_name);
        snprintf(dst_path, PATH_MAX, "%s/%s", target, entry->d_name);

        if (stat(src_path, &st_src) == 0 && S_ISREG(st_src.st_mode)) {
            if (stat(dst_path, &st_dst) != 0) {
                copy_file(src_path, dst_path, &st_src);
            } else if (st_src.st_mtime > st_dst.st_mtime) {
                copy_file(src_path, dst_path, &st_src);
            }
        }
    }
    closedir(dir);

    dir = opendir(target);
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        snprintf(src_path, PATH_MAX, "%s/%s", source, entry->d_name);
        snprintf(dst_path, PATH_MAX, "%s/%s", target, entry->d_name);

        if (stat(dst_path, &st_dst) == 0 && S_ISREG(st_dst.st_mode)) {
            if (stat(src_path, &st_src) != 0) {
                unlink(dst_path);
                syslog(LOG_INFO, "Usunieto plik: %s", dst_path);
            }
        }
    }
    closedir(dir);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uzycie: %s <zrodlo> <cel> [sekundy]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *source = argv[1];
    char *target = argv[2];
    int sleep_time = (argc > 3) ? atoi(argv[3]) : 300;

    struct stat st_src, st_dst;
    if (stat(source, &st_src) != 0 || !S_ISDIR(st_src.st_mode) ||
        stat(target, &st_dst) != 0 || !S_ISDIR(st_dst.st_mode)) {
        fprintf(stderr, "Blad: Sciezki musza byc katalogami.\n");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    signal(SIGUSR1, sigusr1_handler);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");
    
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    openlog("FolderSyncDaemon", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Demon uruchomiony. Zrodlo: %s, Cel: %s, Czas: %ds", source, target, sleep_time);

    while (1) {
        syslog(LOG_INFO, "Demon budzi sie i rozpoczyna synchronizacje.");
        synchronize_folders(source, target);
        
        syslog(LOG_INFO, "Demon zasypia na %d sekund.", sleep_time);
        
        int remaining = sleep_time;
        while (remaining > 0 && !wakeup_requested) {
            remaining = sleep(remaining);
        }

        if (wakeup_requested) {
            syslog(LOG_INFO, "Demon wybudzony sygnalem SIGUSR1.");
            wakeup_requested = 0;
        }
    }

    closelog();
    return 0;
}
