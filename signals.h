#ifndef SIGNALS_H
#define SIGNALS_H

#include <signal.h>

/*
 * Flaga ustawiana przez handler SIGUSR1.
 * Zadeklarowana jako extern – definicja znajduje się w signals.c.
 * Odczytywana i zerowana w głównej pętli demona (main.c).
 */
extern volatile sig_atomic_t wakeup_requested;

/*
 * Instaluje handler dla sygnału SIGUSR1.
 * Należy wywołać po daemonize(), zanim demon zacznie spać.
 */
void setup_signals(void);

#endif /* SIGNALS_H */
