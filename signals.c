#include "signals.h"

#include <string.h>
#include <unistd.h>

/* Definicja zmiennej – widoczna w całym programie przez extern w signals.h */
volatile sig_atomic_t wakeup_requested = 0;

static void sigusr1_handler(int sig) {
    (void)sig;          /* unikamy ostrzeżenia kompilatora o nieużywanym parametrze */
    wakeup_requested = 1;
}

void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
}
