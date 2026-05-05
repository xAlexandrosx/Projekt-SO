#ifndef SYNC_H
#define SYNC_H

#include <sys/types.h>  /* off_t */
#include <stdbool.h>

/*
 * Synchronizuje katalog source z katalogiem target:
 *
 *   Przebieg 1 (source → target):
 *     - Plik z source brak w target          → kopiuj
 *     - Plik z source nowszy niż w target    → kopiuj
 *     - Katalog z source brak w target + -R  → utwórz mkdir + rekurencja
 *
 *   Przebieg 2 (usuwanie z target):
 *     - Plik w target, brak w source         → unlink
 *     - Katalog w target, brak w source + -R → usuń rekurencyjnie
 *
 * Pozycje nie będące zwykłymi plikami ani katalogami (dowiązania symboliczne,
 * urządzenia itp.) są ignorowane w obu przebiegach.
 *
 * Parametry:
 *   source    – ścieżka katalogu źródłowego (wzorzec)
 *   target    – ścieżka katalogu docelowego (kopia)
 *   recursive – czy synchronizować podkatalogi rekurencyjnie
 *   threshold – próg rozmiaru (bajty) decydujący o metodzie kopiowania
 */
void synchronize_folders(const char *source, const char *target,
                         bool recursive, off_t threshold);

#endif /* SYNC_H */
