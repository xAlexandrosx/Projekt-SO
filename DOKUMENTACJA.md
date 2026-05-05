# Dokumentacja: Demon synchronizacji folderów `foldersync`

---

## Spis treści

1. [Co to jest demon?](#1-co-to-jest-demon)
2. [Jak powstaje demon w Linuksie](#2-jak-powstaje-demon-w-linuksie)
3. [Struktura projektu](#3-struktura-projektu)
4. [Architektura i przepływ danych](#4-architektura-i-przepływ-danych)
5. [Opis modułów](#5-opis-modułów)
   - 5.1 [daemon.h / daemon.c](#51-daemonh--daemonc)
   - 5.2 [signals.h / signals.c](#52-signalsh--signalsc)
   - 5.3 [copy.h / copy.c](#53-copyh--copyc)
   - 5.4 [sync.h / sync.c](#54-synch--syncc)
   - 5.5 [main.c](#55-mainc)
   - 5.6 [Makefile](#56-makefile)
6. [Logika synchronizacji](#6-logika-synchronizacji)
7. [Systemowy dziennik zdarzeń (syslog)](#7-systemowy-dziennik-zdarzeń-syslog)
8. [Kompilacja i uruchomienie](#8-kompilacja-i-uruchomienie)
9. [Przykłady użycia](#9-przykłady-użycia)
10. [Często zadawane pytania](#10-często-zadawane-pytania)

---

## 1. Co to jest demon?

**Demon** (ang. *daemon*) to proces działający w tle systemu operacyjnego Linux/Unix,
który:

- uruchamia się raz i działa nieprzerwanie, bez interakcji z użytkownikiem,
- jest całkowicie **odłączony od terminala** — zamknięcie konsoli nie przerywa jego pracy,
- nie korzysta ze standardowego wejścia/wyjścia (`stdin`, `stdout`, `stderr`),
- wysyła wszystkie komunikaty do **systemowego dziennika** (`syslog`), a nie na ekran.

Przykłady demonów w Linuksie:

| Demon     | Zadanie                                      |
|-----------|----------------------------------------------|
| `sshd`    | Nasłuchuje połączeń SSH                      |
| `cron`    | Uruchamia zadania według harmonogramu        |
| `nginx`   | Obsługuje żądania HTTP                       |
| `systemd` | Zarządza wszystkimi innymi usługami systemu  |

Nasz program `foldersync` jest demonem: startuje jednorazowo z terminala, znika
w tle i bez przerwy synchronizuje dwa wskazane katalogi.

---

## 2. Jak powstaje demon w Linuksie

Tworzenie demona wymaga wykonania ściśle określonej sekwencji kroków. Realizuje
ją funkcja `daemonize()` z modułu `daemon.c`.

### Krok 1 — Pierwsze `fork()`

```c
pid_t pid = fork();
if (pid > 0) exit(EXIT_SUCCESS);   // rodzic kończy działanie
```

`fork()` tworzy kopię bieżącego procesu. **Rodzic natychmiast kończy działanie** —
shell widzi, że program zakończył się, i zwraca prompt użytkownikowi. Dziecko
kontynuuje jako „osierocony" proces.

### Krok 2 — `setsid()`

```c
setsid();
```

`setsid()` tworzy **nową sesję** i czyni dziecko jej liderem. Sesja to grupa
procesów powiązanych z jednym terminalem. Nowa sesja nie ma żadnego terminala —
demon nie otrzyma `SIGHUP` gdy użytkownik zamknie konsolę.

### Krok 3 — Drugie `fork()`

```c
pid = fork();
if (pid > 0) exit(EXIT_SUCCESS);
```

Lider sesji mógłby teoretycznie ponownie uzyskać terminal przez otwarcie
urządzenia `/dev/ttyX`. Drugi `fork()` sprawia, że demon **nie jest liderem sesji**
i ten problem znika na stałe.

### Krok 4 — `umask(0)`

```c
umask(0);
```

`umask` to maska domyślnie odejmowana od uprawnień przy tworzeniu plików.
Ustawiając ją na `0` mówimy: „chcę pełną kontrolę nad prawami — nie odejmuj nic
bez mojej wiedzy".

### Krok 5 — `chdir("/")`

```c
chdir("/");
```

Gdyby demon pozostał w katalogu startowym, system plików zawierający ten katalog
nie mógłby zostać odmontowany (zwróciłby błąd `device busy`). Katalog główny `/`
nigdy nie jest odmontowywany — jest bezpieczny.

### Krok 6 — Przekierowanie deskryptorów na `/dev/null`

```c
int devnull = open("/dev/null", O_RDWR);
dup2(devnull, STDIN_FILENO);
dup2(devnull, STDOUT_FILENO);
dup2(devnull, STDERR_FILENO);
```

`/dev/null` to specjalny plik — pochłania wszystko co się do niego zapisze, a przy
czytaniu zwraca natychmiast EOF. Przekierowanie standardowych deskryptorów
zapobiega przypadkowemu wypisaniu czegoś na terminal lub błędom przy próbie
odczytu od użytkownika.

### Tabela — stan procesu przed i po `daemonize()`

| Właściwość            | Przed `daemonize()` | Po `daemonize()` |
|-----------------------|---------------------|------------------|
| Powiązany z terminalem| Tak                 | Nie              |
| Rodzic procesu        | Shell               | init / systemd   |
| Katalog roboczy       | Dowolny             | `/`              |
| stdin / stdout        | Terminal            | `/dev/null`      |
| Komunikaty            | Ekran               | syslog           |

---

## 3. Struktura projektu

```
foldersync/
│
├── Makefile        ← reguły budowania; definicja flag kompilatora
│
├── main.c          ← punkt wejścia: parsowanie argumentów, pętla główna
│
├── daemon.h        ← deklaracja daemonize()
├── daemon.c        ← implementacja: double-fork, setsid, /dev/null
│
├── signals.h       ← deklaracja setup_signals() + extern wakeup_requested
├── signals.c       ← implementacja: handler SIGUSR1
│
├── copy.h          ← deklaracja copy_file()
├── copy.c          ← implementacja: copy_rw(), copy_mmap(), copy_file()
│
├── sync.h          ← deklaracja synchronize_folders()
└── sync.c          ← implementacja: synchronize_folders(), remove_recursive()
```

### Dlaczego taki podział?

Każdy moduł odpowiada za **jedną, dobrze zdefiniowaną odpowiedzialność**:

| Moduł     | Odpowiedzialność                                               |
|-----------|----------------------------------------------------------------|
| `daemon`  | Wie jak zostać demonem — nic więcej                           |
| `signals` | Wie jak obsłużyć SIGUSR1 — nic więcej                        |
| `copy`    | Wie jak kopiować plik (rw lub mmap) — nic więcej             |
| `sync`    | Wie jak porównać i zsynchronizować dwa katalogi               |
| `main`    | Scala wszystko: argumenty → demon → pętla → syslog            |

Taki podział pozwala:
- **testować** każdy moduł niezależnie,
- **rozwijać** jeden moduł bez ryzyka wpłynięcia na inne,
- **czytać** kod — każdy plik ma jasny, wąski cel.

---

## 4. Architektura i przepływ danych

```
Uruchomienie z terminala
        │
        ▼
    main()
        │
        ├─ getopt()               parsuje -R, -t, pozycyjne
        ├─ stat()                 sprawdza czy source i target istnieją
        │
        ├─ daemonize()  ◄─────── [daemon.c]
        │       ├── fork #1  → rodzic kończy, shell odzyskuje prompt
        │       ├── setsid() → nowa sesja, brak terminala
        │       ├── fork #2  → demon nie jest liderem sesji
        │       ├── umask(0), chdir("/")
        │       └── stdin/stdout/stderr → /dev/null
        │
        ├─ setup_signals()  ◄──── [signals.c]  instaluje handler SIGUSR1
        ├─ openlog()                            podłącza się do syslogd
        │
        └─ while(1)  ══ GŁÓWNA PĘTLA ══════════════════════════════════
                │
                ├─ synchronize_folders()  ◄───── [sync.c]
                │       │
                │       ├─ Przebieg 1 (source → target)
                │       │       ├─ lstat() każdego wpisu w source
                │       │       ├─ S_ISREG → copy_file() jeśli brak/nowszy
                │       │       └─ S_ISDIR + -R → mkdir + rekurencja
                │       │
                │       └─ Przebieg 2 (target → czyszczenie)
                │               ├─ lstat() każdego wpisu w target
                │               ├─ brak w source + S_ISREG → unlink()
                │               └─ brak w source + S_ISDIR + -R → remove_recursive()
                │
                │       copy_file()  ◄─────────── [copy.c]
                │               ├─ rozmiar < próg → copy_rw()   (read/write, 4KB)
                │               ├─ rozmiar ≥ próg → copy_mmap() (mmap całego pliku)
                │               └─ utime() → przywrócenie mtime ze źródła
                │
                ├─ sleep(sleep_sec)  ◄── przerywalny przez SIGUSR1
                │       │
                │       └─ wakeup_requested == 1?  ◄── [signals.c]
                │               TAK → zeruj flagę, wróć na górę pętli
                │               NIE → śpij dalej
                │
                └─ (powtarzaj bez końca)
```

---

## 5. Opis modułów

### 5.1 `daemon.h` / `daemon.c`

**Plik nagłówkowy** deklaruje jedyną publiczną funkcję modułu:

```c
void daemonize(void);
```

Komentarz w nagłówku opisuje wszystkich 6 kroków procedury — dzięki temu
wywołujący (tu: `main.c`) nie musi zaglądać do `.c`, żeby zrozumieć co się dzieje.

**Plik źródłowy** implementuje te 6 kroków (opisane szczegółowo w rozdziale 2).
Ważna decyzja projektowa: błędy są zgłaszane przez `perror()` + `exit()`, bo
`daemonize()` jest wywoływana **przed** `openlog()` — syslog jeszcze nie działa,
a stderr jest wciąż podłączony do terminala.

---

### 5.2 `signals.h` / `signals.c`

**Nagłówek** eksportuje dwie rzeczy:

```c
extern volatile sig_atomic_t wakeup_requested;   // flaga, którą czyta main.c
void setup_signals(void);                         // instaluje handler SIGUSR1
```

Słowo kluczowe `extern` przy zmiennej oznacza: „ta zmienna *istnieje* w innym
pliku `.c` (tu: `signals.c`), tylko ją tu *deklaruję*, nie definiuję". Dzięki
temu `main.c` może odczytywać i zerować `wakeup_requested` bez osobnej funkcji
gettera.

**Plik źródłowy** definiuje faktyczną zmienną i handler:

```c
volatile sig_atomic_t wakeup_requested = 0;

static void sigusr1_handler(int sig) {
    (void)sig;
    wakeup_requested = 1;
}
```

`static` przy handlerze oznacza, że jest widoczny **tylko** w tym pliku — to
właściwe ukrywanie szczegółów implementacji.

`setup_signals()` używa `sigaction()` zamiast starszego `signal()`:
`sigaction()` ma deterministyczne zachowanie (np. gwarantuje że handler nie
jest automatycznie resetowany po pierwszym wywołaniu), co jest wymagane
w środowiskach produkcyjnych.

---

### 5.3 `copy.h` / `copy.c`

**Nagłówek** eksportuje jedną funkcję:

```c
void copy_file(const char *src_path, const char *dst_path,
               const struct stat *st, off_t threshold);
```

Przekazanie `struct stat *st` (zamiast samego rozmiaru) pozwala funkcji jednocześnie
ustawić prawa dostępu pliku docelowego (`st->st_mode`) i przywrócić datę
modyfikacji (`st->st_mtime`) — bez dodatkowego wywołania `stat()`.

**Plik źródłowy** zawiera trzy funkcje, z których dwie są prywatne (`static`):

#### `copy_rw()` — metoda read/write (małe pliki)

```c
static void copy_rw(int src_fd, int dst_fd, const char *src_path) {
    char buf[4096];
    ssize_t n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(dst_fd, buf + written, (size_t)(n - written));
            written += w;
        }
    }
}
```

Czytamy 4 KB naraz i zapisujemy do pliku docelowego. Wewnętrzna pętla `while
(written < n)` jest konieczna: `write()` może zapisać **mniej** niż żądano
(szczególnie na sieciowych systemach plików) — wtedy dosyłamy resztę.

#### `copy_mmap()` — metoda mmap/write (duże pliki)

```c
static void copy_mmap(int src_fd, int dst_fd, off_t size, const char *src_path) {
    void *mapped = mmap(NULL, size, PROT_READ, MAP_SHARED, src_fd, 0);
    // ...
    write(dst_fd, mapped, size);
    munmap(mapped, size);
}
```

`mmap()` **odwzorowuje** plik źródłowy bezpośrednio w przestrzeń adresową procesu.
Po wywołaniu `mmap` plik jest dostępny jak zwykła tablica bajtów w pamięci — jądro
samo decyduje kiedy i które strony (bloki po 4 KB) załadować z dysku. Jest to
efektywniejsze dla dużych plików, bo eliminuje zbędne kopiowanie przez bufor
w przestrzeni użytkownika.

Jeśli `mmap()` zawiedzie (np. brak pamięci wirtualnej, niekompatybilny system
plików), funkcja **automatycznie przełącza się** na `copy_rw()` i zapisuje
ostrzeżenie do syslog.

#### `copy_file()` — publiczny interfejs

Otwiera deskryptory, wybiera metodę i na końcu **przywraca datę modyfikacji**:

```c
struct utimbuf times = { .actime = st->st_atime, .modtime = st->st_mtime };
utime(dst_path, &times);
```

To kluczowy krok: gdybyśmy nie przywrócili `mtime`, przy każdym obudzeniu
skopiowany plik miałby bieżącą datę — nowszą niż źródło — i demon nigdy by go
nie skopiował ponownie. Ale gdyby plik w katalogu źródłowym *naprawdę* się zmienił,
nowa data modyfikacji byłaby późniejsza i demon poprawnie wykryłby zmianę.

---

### 5.4 `sync.h` / `sync.c`

**Nagłówek** eksportuje jedną funkcję:

```c
void synchronize_folders(const char *source, const char *target,
                         bool recursive, off_t threshold);
```

**Plik źródłowy** zawiera dwie funkcje:

#### `remove_recursive()` — prywatna

Odpowiednik `rm -rf`. Przechodzi drzewo katalogów algorytmem DFS (Depth-First
Search — najpierw wchodzi w głąb, potem usuwa):

```
remove_recursive("/cel/stary_katalog")
  ├── remove_recursive("/cel/stary_katalog/podkatalog_A")
  │       ├── unlink("plik1.txt")
  │       └── rmdir("podkatalog_A")   ← teraz pusty
  ├── unlink("plik2.txt")
  └── rmdir("stary_katalog")          ← teraz pusty
```

`rmdir()` wymaga, żeby katalog był pusty — dlatego najpierw usuwamy zawartość.

Ważne: pomijamy wpisy `.` i `..` — bez tego rekurencja wchodziłaby w nieskończoność
i skasowałaby cały dysk.

#### `synchronize_folders()` — publiczna

Wykonuje **dwa przejścia** po katalogach:

**Przejście 1 — kopiowanie ze source do target:**

```c
dir = opendir(source);
while ((entry = readdir(dir)) != NULL) {
    lstat(src_path, &st_src);

    if (S_ISREG(st_src.st_mode)) {          // zwykły plik
        if (lstat(dst_path, &st_dst) != 0)  // brak w target
            copy_file(...);
        else if (st_src.st_mtime > st_dst.st_mtime)  // source nowszy
            copy_file(...);

    } else if (recursive && S_ISDIR(st_src.st_mode)) { // katalog + -R
        mkdir(dst_path, ...);
        synchronize_folders(src_path, dst_path, ...); // rekurencja
    }
    // wszystko inne (dowiązania, urządzenia) → ignorowane
}
```

Używamy `lstat()` zamiast `stat()`, bo `stat()` podąża za dowiązaniami symbolicznymi
i poinformowałby nas o właściwościach *celu* dowiązania, a nie samego dowiązania.
Chcemy je **ignorować** — musimy je najpierw poprawnie zidentyfikować.

**Przejście 2 — usuwanie z target tego, czego nie ma w source:**

```c
dir = opendir(target);
while ((entry = readdir(dir)) != NULL) {
    if (lstat(src_path, &st_src) != 0) {    // nie ma w source
        if (S_ISREG(st_dst.st_mode))
            unlink(dst_path);
        else if (recursive && S_ISDIR(st_dst.st_mode))
            remove_recursive(dst_path);
    }
}
```

---

### 5.5 `main.c`

Punkt wejścia i klej łączący wszystkie moduły. Odpowiada za trzy rzeczy:

#### Parsowanie argumentów — `getopt()`

```c
while ((opt = getopt(argc, argv, "Rt:h")) != -1) {
    case 'R': recursive = true;
    case 't': threshold = atol(optarg);
    case 'h': usage(); return EXIT_SUCCESS;
}
char *source = argv[optind];
char *target = argv[optind + 1];
int sleep_sec = (optind + 2 < argc) ? atoi(argv[optind + 2]) : DEFAULT_SLEEP_SECS;
```

`getopt()` to standardowa funkcja POSIX do parsowania opcji w stylu Unix.
- `"Rt:h"` — `R` to flaga bez argumentu, `t:` wymaga argumentu, `h` to pomoc.
- Po przetworzeniu flag `optind` wskazuje pierwszy argument pozycyjny (ścieżki).

#### Weryfikacja ścieżek przed `daemonize()`

```c
if (stat(source, &st) != 0 || !S_ISDIR(st.st_mode)) {
    fprintf(stderr, "Blad: ...");
    return EXIT_FAILURE;
}
```

Celowo robimy to **przed** staniem się demonem — po `daemonize()` `stderr`
trafia do `/dev/null`, więc użytkownik nie zobaczyłby błędu.

#### Główna pętla demona

```c
while (1) {
    synchronize_folders(source, target, recursive, threshold);

    int remaining = sleep_sec;
    while (remaining > 0 && !wakeup_requested)
        remaining = (int)sleep((unsigned int)remaining);

    if (wakeup_requested) { wakeup_requested = 0; }
}
```

`sleep()` zwraca liczbę **pozostałych sekund**, jeśli zostanie przerwany przez
sygnał. Zewnętrzna pętla wznawia sen, o ile nie to był nasz `SIGUSR1`.

---

### 5.6 `Makefile`

```makefile
CC     = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L
TARGET = foldersync
SRCS   = main.c daemon.c signals.c copy.c sync.c
OBJS   = $(SRCS:.c=.o)
```

Kluczowe decyzje:

| Element                       | Uzasadnienie                                                            |
|-------------------------------|-------------------------------------------------------------------------|
| `-std=c11`                    | Wymusza standard C11 — nowoczesny, przenośny                           |
| `-D_POSIX_C_SOURCE=200809L`   | Odkrywa POSIX API (`getopt`, `sigaction`, `off_t`) w trybie `-std=c11` |
| `-Wall -Wextra -Wpedantic`    | Maksimum ostrzeżeń — wyłapuje błędy przed uruchomieniem               |
| `$(SRCS:.c=.o)`               | Automatycznie generuje listę plików `.o` z listy `.c`                  |
| `$(HEADERS)` jako zależność   | Recompiluje każdy `.c` gdy zmieni się **dowolny** nagłówek             |

Reguła kompilacji każdego modułu niezależnie umożliwia **kompilację przyrostową**:
po zmianie `copy.c` Makefile skompiluje tylko `copy.o` i zlinkuje na nowo —
pozostałe pliki `.o` pozostają bez zmian.

---

## 6. Logika synchronizacji

### Kierunek synchronizacji

Synchronizacja jest **jednostronna**: `source` → `target`.

- `source` to **wzorzec** — demon mu bezgranicznie ufa.
- `target` to **kopia** — demon ją dostosowuje do source.

Pliki dodane ręcznie do `target` bez odpowiednika w `source` zostaną **usunięte**
przy następnym obudzeniu.

### Drzewo decyzyjne dla każdego wpisu

```
Wpis znaleziony w source
       │
       ├─ S_ISREG (zwykły plik)?
       │       ├─ Brak w target                     → KOPIUJ
       │       ├─ W target, ale source nowszy (mtime)→ KOPIUJ
       │       └─ W target, mtime identyczny         → nic
       │
       ├─ S_ISDIR (katalog) + flaga -R?
       │       ├─ Brak w target                     → mkdir + rekurencja
       │       └─ Jest w target                     → rekurencja
       │
       └─ Cokolwiek innego (dowiązanie, urządzenie) → IGNORUJ


Wpis znaleziony w target, BRAK w source
       ├─ S_ISREG                                   → unlink()
       └─ S_ISDIR + flaga -R                        → remove_recursive()
```

### Dlaczego `mtime` jako kryterium zmiany?

Porównanie `st_src.st_mtime > st_dst.st_mtime` zamiast np. sum kontrolnych (MD5)
jest **celową kompromisem**:

- Porównanie sum kontrolnych wymagałoby odczytania **całego** pliku przy każdym
  obudzeniu — dla dużych plików byłoby to bardzo kosztowne.
- `mtime` jest aktualizowany przez system operacyjny przy każdym zapisie — jest
  wystarczająco wiarygodnym wskaźnikiem zmiany dla zadań synchronizacyjnych.
- Trick `utime()` po kopiowaniu ustawia `mtime` kopii identycznie jak źródło,
  więc przy kolejnym obudzeniu warunek `>` nie jest spełniony — demon nie kopiuje
  bez powodu.

---

## 7. Systemowy dziennik zdarzeń (syslog)

Demon komunikuje się z administratorem przez `syslog`. Wszystkie zdarzenia są
logowane z identyfikatorem `FolderSyncDaemon`.

```c
openlog("FolderSyncDaemon", LOG_PID | LOG_NDELAY, LOG_DAEMON);
syslog(LOG_INFO, "Demon uruchomiony...");
```

### Poziomy ważności

| Poziom       | Kiedy używany                                 |
|--------------|-----------------------------------------------|
| `LOG_ERR`    | Błąd — operacja się nie powiodła              |
| `LOG_WARNING`| Ostrzeżenie — kontynuujemy, ale coś jest nie tak |
| `LOG_INFO`   | Standardowe zdarzenie (kopiowanie, sleep, wake)|

### Odczyt logów

```bash
# Systemy z systemd (Ubuntu, Debian, Fedora):
journalctl -t FolderSyncDaemon -f

# Klasyczny syslog:
grep FolderSyncDaemon /var/log/syslog | tail -30

# W czasie rzeczywistym:
tail -f /var/log/syslog | grep FolderSyncDaemon
```

### Przykładowe wpisy

```
May  5 18:00:01 host FolderSyncDaemon[4231]: Demon uruchomiony. Zrodlo: /home/user/src, Cel: /home/user/dst, Czas: 60s, Rekurencja: tak, Prog mmap: 102400 B
May  5 18:00:01 host FolderSyncDaemon[4231]: Demon wybudzony – rozpoczynam synchronizacje.
May  5 18:00:01 host FolderSyncDaemon[4231]: Kopiowanie (read/write) [    512 B]: /home/user/src/config.txt -> /home/user/dst/config.txt
May  5 18:00:01 host FolderSyncDaemon[4231]: Kopiowanie (mmap)       [5242880 B]: /home/user/src/backup.tar -> /home/user/dst/backup.tar
May  5 18:00:03 host FolderSyncDaemon[4231]: Usunieto plik: /home/user/dst/stary.log
May  5 18:00:03 host FolderSyncDaemon[4231]: Synchronizacja zakonczona. Demon zasypia na 60 sekund.
May  5 18:01:03 host FolderSyncDaemon[4231]: Demon wybudzony sygnalem SIGUSR1.
May  5 18:01:03 host FolderSyncDaemon[4231]: Demon wybudzony – rozpoczynam synchronizacje.
```

Data i godzina są dodawane automatycznie przez `syslogd`.

---

## 8. Kompilacja i uruchomienie

### Kompilacja

```bash
# Wejdź do katalogu projektu
cd foldersync/

# Zbuduj (kompilacja przyrostowa)
make

# Pełna przebudowa od zera
make clean && make
```

Po kompilacji powstaje plik wykonywalny `foldersync`.

### Składnia polecenia

```bash
./foldersync [-R] [-t BAJTY] <source> <target> [sekundy]
```

| Argument      | Wymagany | Opis                                          |
|---------------|----------|-----------------------------------------------|
| `<source>`    | Tak      | Katalog źródłowy (wzorzec)                    |
| `<target>`    | Tak      | Katalog docelowy (kopia)                      |
| `[sekundy]`   | Nie      | Interwał synchronizacji (domyślnie: 300)      |
| `-R`          | Nie      | Rekurencyjna synchronizacja podkatalogów      |
| `-t BAJTY`    | Nie      | Próg mmap w bajtach (domyślnie: 102400)       |
| `-h`          | Nie      | Wyświetla pomoc i kończy                      |

### Zarządzanie demonem po uruchomieniu

```bash
# Znajdź PID uruchomionego demona
pgrep -a foldersync

# Natychmiastowa synchronizacja (bez czekania na timer)
kill -USR1 <PID>

# Zatrzymaj demona (eleganckie zakończenie)
kill <PID>

# Wymuś zatrzymanie (jeśli nie reaguje)
kill -9 <PID>
```

---

## 9. Przykłady użycia

### Przykład 1 — Podstawowa synchronizacja co 10 sekund

```bash
mkdir -p /tmp/src /tmp/dst
./foldersync /tmp/src /tmp/dst 10
```

### Przykład 2 — Rekurencyjna synchronizacja z domyślnym timerem (5 min)

```bash
./foldersync -R /home/user/dokumenty /mnt/backup
```

### Przykład 3 — Pełne opcje: rekurencja, próg 1 MB, co 2 minuty

```bash
./foldersync -R -t 1048576 /home/user/dokumenty /mnt/backup 120
```

### Przykład 4 — Kompletny test z weryfikacją

```bash
# Przygotowanie
mkdir -p /tmp/src/podfolder /tmp/dst
echo "plik testowy" > /tmp/src/plik.txt
echo "zagniezdzone" > /tmp/src/podfolder/zagniezdz.txt

# Uruchomienie (rekurencja, próg 50 KB, obudzenie co 30 sekund)
./foldersync -R -t 51200 /tmp/src /tmp/dst 30

# Po chwili sprawdzamy czy pliki zostały skopiowane
sleep 1
find /tmp/dst -type f
# Oczekiwany wynik:
# /tmp/dst/plik.txt
# /tmp/dst/podfolder/zagniezdz.txt

# Modyfikujemy plik i wymuszamy natychmiastową synchronizację
echo "zmiana" >> /tmp/src/plik.txt
kill -USR1 $(pgrep foldersync)
sleep 1
diff /tmp/src/plik.txt /tmp/dst/plik.txt  # brak różnic

# Usuwamy plik ze źródła i wymuszamy sync
rm /tmp/src/plik.txt
kill -USR1 $(pgrep foldersync)
sleep 1
ls /tmp/dst/  # plik.txt powinien zniknąć

# Sprzątanie
pkill foldersync
rm -rf /tmp/src /tmp/dst
```

---

## 10. Często zadawane pytania

**Q: Dlaczego `lstat()` zamiast `stat()`?**

`stat()` podąża za dowiązaniami symbolicznymi i podaje właściwości *pliku docelowego*
dowiązania, a nie samego dowiązania. W efekcie dowiązania byłyby traktowane
jak zwykłe pliki i kopiowane (a my chcemy je ignorować). `lstat()` podaje
właściwości samego dowiązania, co pozwala je poprawnie zidentyfikować i pominąć.

**Q: Co jeśli source lub target przestanie być dostępny w trakcie działania demona?**

`opendir()` zwróci `NULL`. Demon zapisze błąd do syslog i pominie bieżący cykl
synchronizacji. Przy następnym obudzeniu spróbuje ponownie.

**Q: Czy mmap może zawieść?**

Tak — np. przy braku pamięci wirtualnej lub na niektórych sieciowych systemach
plików (NFS, CIFS). Kod obsługuje to automatycznie: `if (mapped == MAP_FAILED)`
przełącza na `copy_rw()` i zapisuje ostrzeżenie `LOG_WARNING` do syslog.

**Q: Co z plikami modyfikowanymi w trakcie kopiowania?**

Kopia może być niespójna (część starych danych, część nowych). Obsługa tego
przypadku wymagałaby blokad pliku (`fcntl` / `flock`) i wykracza poza zakres
tego projektu.

**Q: Jak znaleźć PID demona żeby wysłać SIGUSR1?**

```bash
pgrep -a foldersync            # lista procesów o tej nazwie
pgrep -f "foldersync.*dst"     # filtrowanie po fragmencie komendy
```

W systemach produkcyjnych demon powinien zapisywać swój PID do pliku
`/var/run/foldersync.pid` — umożliwiłoby to skryptom `init.d` / `systemd`
eleganckie zarządzanie procesem.
