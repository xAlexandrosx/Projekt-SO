#ifndef COPY_H
#define COPY_H

#include <sys/stat.h>   /* struct stat, off_t */

/*
 * Kopiuje plik z src_path do dst_path zachowując prawa dostępu
 * i datę modyfikacji z metadanych *st.
 *
 * Wybór metody kopiowania na podstawie rozmiaru pliku:
 *   rozmiar <  threshold  →  read/write  (bufor 4 KB)
 *   rozmiar >= threshold  →  mmap/write  (plik mapowany w całości)
 *
 * W przypadku niepowodzenia mmap automatycznie przełącza się na read/write
 * i zapisuje ostrzeżenie do syslog.
 */
void copy_file(const char *src_path, const char *dst_path,
               const struct stat *st, off_t threshold);

#endif /* COPY_H */
