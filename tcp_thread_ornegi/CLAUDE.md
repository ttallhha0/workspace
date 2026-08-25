# CLAUDE.md — RadarIPC_System

Guidance for LLM agents working in this repository. Read this before editing anything.

---

## 1. What this project is

`RadarIPC_System` is a **C++17 IPC benchmarking harness disguised as a radar simulation**. It
moves the *same* large payload (size chosen at runtime — 1 MB, 10 MB, 200 MB, anything) between
two processes using **five different IPC transports**, and measures latency, throughput, packet
loss, delivery gaps and data integrity for each.

On top of the transport benchmark sits a small but genuine **radar interpretation** layer: the
sender emits a raw contact (x/y/speed) with an optional IFF transponder code embedded in the
payload; the receiver derives range, bearing, and a friend/foe identification from it. The sender
never knows the identity — interpretation happens strictly on the receiving side.

It is a **learning / internship project**, not a production system. Everything runs on one
machine, on loopback or shared memory. There is no authentication, no serialization format, and
no network-hostile-input hardening — and that is by design.

**Two things to internalize before writing code here:**

1. **The entire codebase is in Turkish.** Identifiers, comments, console output, file names, CSV
   column headers. See the glossary in §10 and the conventions in §9. Match it — do not
   "helpfully" rename things to English.
2. **`README.md` is stale.** It describes an older, Windows-only, 3-executable version. Trust the
   source, then this file, then `README.md` last. See §11.

---

## 2. Layout

```
CMakeLists.txt              Single build file; defines one INTERFACE lib + 4 executables
apps/
  Yonetici/main.cpp         "Manager" — interactive menu, publishes the run config, then exits
  RadarAnteni/main.cpp      "Radar antenna" — data PRODUCER / server side
  KomutaMerkezi/main.cpp    "Command center" — data CONSUMER / client side
  Benchmark/main.cpp        Standalone automated matrix runner (single process, two threads)
include/RadarIPC/           Header-only library (no .cpp files at all)
  Ana Dosya/                NOTE: directory name contains a SPACE
    RadarIletisimArayuzu.h  Abstract IPC driver interface
    RadarVerisi.h           Wire types: Hedef, RadarBaslik, RadarPaketi, IFF_DOST_KODU
  Avx/AvxYardimci.h         AVX2 checksum + copy, with portable fallback
  IPC/IPC_1_TCP/TcpDriver.h
  IPC/IPC_2_UDP/UdpDriver.h
  IPC/IPC_3_Shared_Memory/SharedMemoryDriver.h
  IPC/IPC_4_RPC/RpcDriver.h
  IPC/IPC_5_PCIe/PcieDriver.h
  CiftTamponluHat.h         2-slot producer/consumer pipeline
  KacakRaporlayici.h        Async drop/miss reporter (background thread + CSV)
  KapanisYonetimi.h         Graceful shutdown, SIGINT routing, SIGPIPE suppression
  ModKontrolKanali.h        Control channel (named pipe / FIFO) + command string parser
  Ortam.h                   Env-var-overridable defaults (IP, ports, RPC channel name)
  PlatformUyum.h            Windows(Winsock) <-> POSIX(BSD sockets) compatibility shim
website/                    Static Turkish explainer site (index.html/style.css/script.js + PNGs)
build/                      STALE — see §11. Do not build into this directory.
*.csv                       Committed sample output from previous runs (see §7)
SON_BULGULAR.md             File-I/O audit report — RECOMMENDATIONS ONLY, not implemented (§11)
```

`include/RadarIPC/Ana Dosya/` has a **space in the directory name**. Quote it in shell commands,
and keep the existing relative include style (`#include "../../Ana Dosya/RadarVerisi.h"`).

---

## 3. Build and run

Header-only library + 4 executables. No external dependencies beyond a C++17 compiler, CMake,
and the system threads library.

```bash
cmake -S . -B build-local        # Ninja optional: add -G Ninja if available
cmake --build build-local
```

**Verified working**: macOS 26.5 / arm64 / AppleClang 21, clean build, all four targets, and a
smoke run of `./Benchmark 3 1 SHM,RPC,TCP` completed successfully.

CMake specifics worth knowing:
- `RadarIPCLibrary` is an `INTERFACE` target (headers only). Executables link it to inherit
  include dirs, compile flags, and platform libs.
- `-mavx2` / `/arch:AVX2` is added **only** when `CMAKE_SYSTEM_PROCESSOR` matches x86/x86_64.
  On Apple Silicon and other ARM targets the flag is skipped and `AvxYardimci.h` compiles its
  portable non-SIMD path (§6).
- Windows links `ws2_32`; non-Apple UNIX links `rt`; all platforms link `Threads::Threads`.
- `cmake_minimum_required(VERSION 3.10)` is old enough to draw a deprecation warning from
  CMake 4.x, but it still configures.

### Running the three-process flow

Three terminals, in any order — `RadarAnteni` and `KomutaMerkezi` block until `Yonetici`
publishes a command, so they may be started first.

```bash
./build-local/RadarAnteni
./build-local/KomutaMerkezi
./build-local/Yonetici        # asks: transport, test type, size, period/count, net settings
```

All CSV output lands in the **current working directory** of the process that writes it — there
is no configurable output directory.

### Running the automated matrix

```bash
./build-local/Benchmark [N] [sizes_mb] [methods]
./build-local/Benchmark 200 1,10,50,200 TCP,UDP,SHM,RPC,PCIE     # defaults
```

`Benchmark` is self-contained: it runs both roles as two threads in one process, using the same
driver classes, and needs no `Yonetici`. It hardcodes `127.0.0.1`, ports 8080/8081, and the RPC
channel `RadarRpcBench` (deliberately different from the interactive flow's `RadarRpcChannel`).
It sends a plain `0x5A`-filled buffer — **no `RadarBaslik`, no checksum, no IFF** — so it measures
pure transport cost.

---

## 4. Architecture

### 4.1 Control channel — how the three processes agree on a config

`Yonetici` collects settings interactively, packs them into a single `|`-delimited string, and
pushes it to the other two processes over `ModKontrolKanali`. Then it exits.

```
MOD_TCP|10|5|127.0.0.1|8080|8081|RadarRpcChannel|0
   0     1  2      3      4    5        6        7
```

| # | Field | Meaning |
|---|---|---|
| 0 | `mod` | `MOD_TCP` \| `MOD_UDP` \| `MOD_SHARED_MEMORY` \| `MOD_RPC` \| `MOD_PCIE` |
| 1 | `boyutMB` | Raw signal payload size in MB (default 10) |
| 2 | `araligMs` | Period: one signal every N ms (default 1000) |
| 3 | `ip` | TCP/UDP address |
| 4 | `tcpPort` | default 8080 |
| 5 | `udpPort` | default 8081 |
| 6 | `rpcKanalAdi` | Named-pipe / AF_UNIX socket base name |
| 7 | `adet` | **0 = periodic streaming mode; >0 = batch benchmark mode** |

`ModKontrol::komutuAyristir()` is backward compatible with shorter forms (`MOD_TCP|10`); missing
fields fall back to `Ortam.h` defaults.

**Design point worth preserving:** network settings are *propagated through the command*, not
read independently from the environment by each process. `Ortam.h` env vars (`RADAR_IP`,
`RADAR_TCP_PORT`, `RADAR_UDP_PORT`, `RADAR_RPC_KANAL`) are only consulted by `Yonetici` and as a
parse-time fallback. If each process read its own env, a mismatch would silently fail to connect.

Transport of the control channel itself:
- **Windows**: one named pipe `\\.\pipe\RadarModKontrol`, written twice sequentially.
- **POSIX**: two FIFOs, `/tmp/RadarModKontrol_Radar` and `/tmp/RadarModKontrol_Komuta`.

### 4.2 The two run modes

Field 7 (`adet`) selects the mode, and **both** `RadarAnteni` and `KomutaMerkezi` derive
`benchmarkMod = (kb.adet > 0)` from the same string, so the two sides always agree.

**Periodic streaming (`adet == 0`)** — a soft real-time task. One signal every `araligMs`.
If the sender can't keep up, the signal is **dropped, not delayed**. Drops are counted and
reported. The console prints *only* drops; throughput data goes to CSV.

**Batch benchmark (`adet > 0`)** — "deliver exactly N signals as fast as possible, measure total
elapsed time." No period, no drops: the producer *blocks* on a full buffer instead of dropping.
This mode also enables the **lossless (ack'd) handshake** in `SharedMemoryDriver` and
`PcieDriver`, since overwriting the single shared buffer would invalidate the measurement.

### 4.3 The IPC driver interface

`RadarIletisimArayuzu` (in `Ana Dosya/RadarIletisimArayuzu.h`) is the contract every transport
implements:

```cpp
virtual bool baslat() = 0;                                  // connect / bind / map
virtual bool gonder(const void* veri, size_t toplamBoyut) = 0;
virtual bool oku(void* veri, size_t toplamBoyut) = 0;
virtual void kapat() = 0;
virtual double sonKayipYuzdesi() const { return 0.0; }      // only UDP overrides this
virtual ~RadarIletisimArayuzu() {}
```

`gonder`/`oku` work on **raw `void*` + byte count**, not a fixed struct type. That is what makes
runtime-selectable payload sizes possible without recompiling.

Drivers are selected by an `if/else` chain over `modKomutu` in each `main.cpp`. Constructor
signatures differ per driver — this is intentional, each takes what it needs:

| Mod | Class | Constructor |
|---|---|---|
| `MOD_TCP` | `TcpDriver` | `(bool serverMi, const char* ip, int port)` |
| `MOD_UDP` | `UdpDriver` | `(bool gondericiMi, const char* ip, int port)` |
| `MOD_SHARED_MEMORY` | `SharedMemoryDriver` | `(bool serverModu, size_t toplamBoyut, bool kayipsizMod)` |
| `MOD_RPC` | `RpcDriver` | `(bool serverMi, size_t toplamBoyut, const std::string& kanalAdi)` |
| `MOD_PCIE` | `PcieDriver` | `(bool donanimMi, size_t toplamBoyut, bool kayipsizMod)` |

### 4.4 The five transports

| # | Mod | Windows impl | POSIX impl | Notes |
|---|---|---|---|---|
| 1 | TCP | Winsock | BSD sockets | 1 MB chunked send/recv loops until complete. `SO_REUSEADDR`. Client re-creates the socket before each retry (BSD requires this — reusing a failed socket for `connect()` is unreliable). |
| 2 | UDP | Winsock | BSD sockets | App-level fragmentation: `FragmentHeader{transferId, fragmentIndex, totalFragments, payloadSize}` + `CHUNK_SIZE = 60000 - sizeof(FragmentHeader)`. 8 MB socket buffers, 2 s read timeout, **deliberately no flow control**. |
| 3 | Shared Memory | `CreateFileMappingA` + `CreateEventA` (`Local\` namespace) | `shm_open` + `mmap` + POSIX named semaphores | Names: `RadarSharedMemory`, `...Event`, `...Ack`. |
| 4 | RPC | Duplex named pipe `\\.\pipe\<kanalAdi>` | `AF_UNIX` `SOCK_STREAM` at `/tmp/<kanalAdi>.sock` | Both sides loop until the full byte count transfers (byte-mode pipes return partial writes). |
| 5 | PCIe | Same primitives as #3, different names | Same as #3, different names | Simulates DMA + hardware interrupt. Names: `RadarPcieRAM`, `RadarPcieInterrupt`, `RadarPcieAck`. |

SHM and PCIe are deliberately near-identical — PCIe is a *narrative reframing* of shared memory
as "FPGA writes to a DMA buffer and raises an interrupt." Keep both; they are separate
measurement subjects and use separate OS object names so they can't collide.

Server side (`RadarAnteni`, and `Benchmark`'s sender thread) `shm_unlink`/`sem_unlink`s before
creating, to clear ghost objects from a previous crashed run. The client side `fstat`s the
segment and waits until it is at least the expected size — otherwise it would map a stale,
undersized segment.

### 4.5 `CiftTamponluHat` — the double-buffered pipeline

Both `RadarAnteni` and `KomutaMerkezi` split their work across two threads with this 2-slot
bounded queue in between:

- `RadarAnteni`: producer = generate signal + compute checksum; consumer = `gonder()`.
- `KomutaMerkezi`: producer = `oku()`; consumer = checksum verify + gap detection + CSV.

Capacity is exactly 2, which gives one buffer of lookahead and natural backpressure.

Two different producer-side acquire methods, and **the choice encodes the run mode**:

- `yazilacakTamponAl()` — **blocks** until a slot frees. Used in batch benchmark mode (no drops).
- `yazilacakTamponAlDenemeli()` — **returns `nullptr` immediately** if both slots are full. Used
  in periodic mode: no free buffer means this period's signal is *dropped*. Pair it with
  `durduruldMu()` to distinguish "pipeline stopped" from "buffers full".

`durdur()` wakes both sides. On the consumer side, `okunacakTamponAl()` drains anything already
queued before returning `nullptr`, so shutdown doesn't lose in-flight data.

### 4.6 Loss accounting — three distinct concepts

This is the subtlest part of the project. Do not conflate these:

**Sender side (`RadarAnteni`, periodic mode), reported to `kacak_uretici.csv`:**
- **(A) `kayipDarbogaz` — "gonderim darbogazi"**: no free buffer at tick time; the IPC transport
  can't keep up.
- **(B) `kayipPeriyot` — "periyot asimi"**: producing the signal (mostly the checksum over a
  large payload) took longer than one period; the schedule skipped one or more slots.

`sinyal_no` is **incremented for every scheduled tick, including dropped ones**, and dropped
signals are never transmitted. That's what makes the receiver's independent detection work.

**Receiver side (`KomutaMerkezi`), reported to `kacak_alici.csv`:**
- **`ulasmayan`** — a *gap* in received `sinyal_no` values: either the sender dropped it or the
  IPC layer lost/overwrote it. Detected purely from sequence numbers.
- **`tekrar`** — a `sinyal_no` lower than expected arrived: the IPC layer delivered a duplicate.
  Duplicates are counted, then skipped (not written to CSV).

Invariant: `ulasan + ulasmayan == highest sinyal_no seen`.

**UDP-specific**: `sonKayipYuzdesi()` reports fragment-level loss *within* the last transfer,
which is orthogonal to the signal-level accounting above.

`KacakRaporlayici` exists because printing and file I/O in a real-time loop would *cause* more
drops — a feedback loop. Real-time threads only `push` a lightweight event onto a queue; a
dedicated reporter thread does the `cout` and CSV writes. A 200 000-event queue cap prevents
unbounded memory growth; the total counter still increments when events are dropped from the
queue. **Preserve this pattern in any new hot-path logging.**

### 4.7 Wire format

```cpp
struct Hedef   { int id; float x, y, hiz; char tip[16]; };   // tip is ALWAYS "BILINMEYEN"
struct RadarBaslik {
    long long timestamp;               // steady_clock microseconds
    uint64_t  sinyal_no;               // monotonic, from 1; gaps == drops
    int       tespit_edilen_hedef_sayisi;
    Hedef     hedefler[5];             // only [0] is ever populated
    uint64_t  veri_checksum;           // AVX checksum of the raw signal
    uint32_t  ham_sinyal_boyutu;
};
struct RadarPaketi { char* tampon; size_t toplamBoyut; /* header + payload, contiguous */ };
```

`RadarPaketi::olustur(uint32_t)` heap-allocates `sizeof(RadarBaslik) + hamSinyalBoyutu` as one
contiguous, zero-initialized block. It is **move/copy-deleted** (single ownership) and frees in
its destructor. The whole block is memcpy'd over the wire verbatim — no serialization, no
endianness handling, no padding normalization. That is fine here because both ends are the same
binary on the same machine, but it means the format is **not portable across machines or ABIs**.

Latency is measured cross-process by writing `steady_clock` microseconds into `timestamp` on the
sender and subtracting on the receiver. This is valid because `steady_clock` shares an epoch
across processes on one machine (`CLOCK_MONOTONIC` on POSIX, QPC on Windows) — and it is the
reason `system_clock` was abandoned (Windows' ~15 ms tick quantized every measurement).

---

## 5. Radar interpretation & IFF

Split of responsibility is deliberate and thematically load-bearing:

- **`RadarAnteni` (sensor)** detects a raw contact and always sets `tip = "BILINMEYEN"`. It never
  assigns identity. With 50% probability it writes `IFF_DOST_KODU`
  (`0x1FF1FF00DEADBEEF`) into the **first 8 bytes of the raw signal**, simulating a friendly
  transponder reply; otherwise those bytes stay zero.
- **`KomutaMerkezi` (interpreter)** runs `radarYorumla()`:
  - `mesafeKm = sqrt(x² + y²)`
  - `aciDerece = atan2(y, x)` in degrees, normalized to 0–360
  - IFF: read the first 8 bytes; exact match → `"DOST"`, anything else (silence or wrong code)
    → `"DUSMAN"`. Real IFF doctrine treats "no reply" as hostile, not friendly.
  - If the checksum failed, identification is refused entirely: `"BILINMEYEN (veri bozuk)"`.
    Never derive an identity from corrupt data.

In batch benchmark mode `RadarAnteni` always writes the IFF code (identity isn't the subject of
that measurement).

---

## 6. AVX2 helpers

`RadarAvx::avxChecksum()` and `RadarAvx::avxKopyala()` in `Avx/AvxYardimci.h`, guarded by
`#if defined(__AVX2__)`.

- **AVX2 path**: 32-byte blocks via `_mm256_loadu_si256` / `_mm256_storeu_si256`; the checksum
  accumulates into four 64-bit lanes with `_mm256_add_epi64`, then folds them with XOR, then
  mixes any tail bytes with `sonuc = sonuc * 131 + byte`.
- **Portable path** (ARM, or x86 built without `-mavx2`): `avxKopyala` is `memcpy`;
  `avxChecksum` reproduces the same four-lane structure with `memcpy` into `uint64_t`s.

The two paths produce **identical checksums on little-endian machines**, so a mixed pair of
processes would still agree. Don't "simplify" the portable path into a single accumulator — that
would break the equivalence.

The project's own honest assessment (in `README.md`, and it is correct): `avxChecksum` is a real
win — integrity checking over 100 MB for nearly free. `avxKopyala` is essentially a wash against
a modern `memcpy` and exists as a demonstration. It is **not** a cryptographic hash; it detects
accidental corruption and truncation, nothing more.

---

## 7. Output files

All written relative to the process's CWD. Sample outputs from earlier runs are committed at the
repo root.

| File | Written by | Mode | Header |
|---|---|---|---|
| `benchmark_sonuclari.csv` | `KomutaMerkezi` (periodic) | append | `mod,boyut_mb,sinyal_no,gecikme_ms,throughput_mbs,checksum_durumu,udp_kayip_yuzdesi,ulasmayan_kumulatif,mesafe_km,aci_derece,kimlik` |
| `benchmark_toplu.csv` | `KomutaMerkezi` (batch) | append | `mod,boyut_mb,adet,ulasan,bozuk,sure_s,per_paket_ms,teslim_mb,hiz_mbs` |
| `benchmark_matris.csv` | `Benchmark` | **truncate** | `mod,boyut_mb,adet,ulasan,sure_s,per_paket_ms,hiz_mbs,kayip_yuzde` |
| `kacak_uretici.csv` | `RadarAnteni` | append | `zaman_ms,kaynak,ilk_sinyal_no,son_sinyal_no,adet,sebep` |
| `kacak_alici.csv` | `KomutaMerkezi` | append | same as above |

Note the `mod` column is inconsistent between writers: the three-process apps write
`MOD_TCP`-style values, `Benchmark` writes short `TCP`/`SHM`/`PCIE`-style values.

---

## 8. Shutdown

`RadarKapanis` (`KapanisYonetimi.h`) provides a process-wide `std::atomic<bool>` flag flipped by
SIGINT, so loops can print a closing summary instead of dying mid-run.

Two non-obvious mechanisms — do not remove them:

1. **`SA_RESTART` is deliberately NOT set** on the SIGINT handler. Blocked `recv()`/`read()`
   calls must return `EINTR` so the receiving thread can wake up on Ctrl+C.
2. **SIGINT is blocked on the main thread and unblocked only on the reader thread**
   (`sinyaliBuThreaddeBlokla()` / `sinyaliBuThreaddeAc()`). In a multithreaded process the kernel
   delivers the signal to an arbitrary non-blocking thread; without this routing, a reader
   blocked in `recv`/`sem_wait` would never be interrupted — which is fatal for SHM/PCIe, where
   there is no "connection closed" event to fall back on.

`SIGPIPE` is set to `SIG_IGN` so a peer disconnect surfaces as `EPIPE` from `send()`/`write()`
instead of killing the process.

`KomutaMerkezi` calls `std::_Exit(0)` at the end of batch benchmark mode. This is intentional:
the reader thread may be parked forever in `WaitForSingleObject`/`sem_wait` for a signal that
will never arrive, and there is no graceful way to wake it. Results are already flushed to disk
before the call.

---

## 9. Conventions to follow

- **Turkish everywhere.** Identifiers, comments, console strings, CSV headers. Never translate
  existing names.
- Classes are `PascalCase`; functions and variables are `camelCase`; header guards are
  `SCREAMING_SNAKE`. Private members appear as either `m_prefix` or `trailing_`; both exist —
  match the surrounding file.
- **Header-only.** There are no `.cpp` files outside `apps/`. New library code goes in a header
  with `inline`/class-inline definitions. Adding a `.cpp` would require changing the `INTERFACE`
  library into a real one — don't, unless explicitly asked.
- **Console output prefers plain ASCII** (`baglanti`, `gonderilemedi`, `Surucu`) so Windows code
  pages don't mangle it. Some older strings still contain diacritics; new ones should not.
- Comments in this codebase are unusually long and explain *why* — often documenting a bug that
  was hit and fixed (BSD `connect()` retry, `Global\` → `Local\`, `system_clock` → `steady_clock`,
  ghost semaphores). This is a teaching codebase; that density is a feature. Preserve and match it.
- Bracketed console tags identify the subsystem: `[TCP]`, `[UDP]`, `[SM]`, `[RPC]`, `[PCIe]`,
  `[KACAK]`, `[BENCHMARK]`.
- Raw `new`/`delete` for drivers, RAII for everything else. Consistent with the existing style;
  don't refactor to smart pointers unasked.

### Adding a sixth IPC transport

1. Create `include/RadarIPC/IPC/IPC_6_<Name>/<Name>Driver.h` implementing
   `RadarIletisimArayuzu`. Override `sonKayipYuzdesi()` only if the transport can actually lose
   data.
2. Guard platform-specific code via `PlatformUyum.h` — **do not include `winsock2.h`/`windows.h`
   directly** in a driver.
3. Add `MOD_<NAME>` to the `if/else` chains in `apps/RadarAnteni/main.cpp` **and**
   `apps/KomutaMerkezi/main.cpp` (server=`true` / client=`false`).
4. Add the menu entry and `switch` case in `apps/Yonetici/main.cpp`.
5. Add a branch to `surucuYap()` in `apps/Benchmark/main.cpp`.
6. If it uses a single shared buffer, support the `kayipsizMod` ack handshake so batch benchmark
   mode stays lossless.
7. No `CMakeLists.txt` change needed — headers are picked up via the include directory.

---

## 10. Turkish glossary

| Turkish | English |
|---|---|
| Yonetici | Manager |
| RadarAnteni | Radar antenna (producer/server) |
| KomutaMerkezi | Command center (consumer/client) |
| uretici / tuketici | producer / consumer |
| gonder / oku / baslat / kapat | send / read / start / close |
| veri / boyut / tampon | data / size / buffer |
| kacak | leak — here: a *missed/dropped* signal |
| sizinti | leakage (receiver-side miss) |
| gecikme | latency |
| kayip / ulasmayan / tekrar | loss / never-arrived / duplicate |
| aralik / periyot / adet | interval / period / count |
| sinyal_no | signal sequence number |
| ham sinyal | raw signal (the large payload) |
| baslik / paket / parca | header / packet / fragment |
| hedef / mesafe / aci | target / range / bearing |
| dost / dusman / bilinmeyen | friend / foe / unknown |
| saglam / bozuk | intact / corrupt |
| kayipsiz | lossless (ack'd handshake mode) |
| darbogaz / asim | bottleneck / overrun |
| surucu / arayuz / hat | driver / interface / pipeline |
| zarif kapanis | graceful shutdown |
| kacak raporlayici | drop reporter |
| yontem / kosu / olcum | method / run / measurement |

---

## 11. Repository state — read this before trusting anything

- **Not a git repository.** No `.git`, no `.gitignore` — despite `README.md` claiming the CSVs
  are gitignored. The CSV outputs are sitting in the working tree.
- **`build/` is stale and foreign.** It contains Windows PE32+ binaries built on a different
  machine (`C:/Users/ERKAN/...`, MinGW GCC 15.2, Ninja). `build/CMakeCache.txt` has hardcoded
  Windows paths. Configuring into `build/` on any other machine will fail or produce confusing
  errors — **always use a fresh directory** (`build-local/`, `build-mac/`, …).
- **`README.md` is out of date.** It describes only 3 executables (no `Benchmark`), calls the
  project Windows-only (it is cross-platform now), documents PCIe as using the `Global\`
  namespace (the code moved to `Local\` precisely to avoid the privilege problem the README
  still lists as a limitation), and shows an old `benchmark_sonuclari.csv` header missing
  `sinyal_no` and `ulasmayan_kumulatif`. It also predates `CiftTamponluHat`, `KacakRaporlayici`,
  `KapanisYonetimi`, `Ortam`, `PlatformUyum`, and batch benchmark mode. Its AVX2 and IFF sections
  are still accurate and genuinely good.
- **`SON_BULGULAR.md` is a proposal document, not a changelog.** Its findings C1–C11 (unchecked
  write errors, locale-dependent decimal separators breaking CSV, no RFC-4180 escaping, header
  TOCTOU races, missing `setprecision`) and P1–P14 (larger buffers, `to_chars`, async writer,
  mmap output, binary format, zstd) are **all still unimplemented**. Do not assume any of it
  landed. It is a reasonable roadmap if someone asks you to improve the I/O layer.
- **`website/`** is an independent static explainer site in Turkish — hero section, architecture
  walkthrough, tabbed IPC comparison, an animated double-buffer visualization, an AVX2 section,
  and a bar chart of results. Vanilla HTML/CSS/JS, no build step, no framework, no dependency on
  the C++ code. Its numbers are hardcoded, not read from the CSVs.

### Known rough edges (observations — do not silently "fix" these)

These are real, but they are consequences of a single-machine learning project. Mention them if
relevant; don't unilaterally rewrite them.

- `UdpDriver::oku()` counts *arrivals*, not distinct fragment indices, so a duplicated fragment
  can prematurely mark a transfer complete. The reassembly buffer is also never cleared between
  transfers, so stale bytes can survive into an incomplete one. The checksum catches the result.
- `UdpDriver::oku()` uses `hdr.fragmentIndex` and `hdr.payloadSize` from the wire without bounds
  validation before the `memcpy` into `reassemblyBuffer`. Any datagram arriving on the UDP port
  is trusted. Fine on loopback; would be a heap overflow with hostile input.
- On a fragment-loss timeout, `oku()` returns `true` with a partially filled buffer, by design —
  the caller learns about it through `sonKayipYuzdesi()` and the checksum, not the return value.
- `RadarPaketi::olustur()` takes a `uint32_t`, and `hamSinyalBoyutu` is cast to `uint32_t` when
  constructing `CiftTamponluHat`. Sizes at or above 4 GB overflow.
- `Yonetici` reads menu input with `std::cin >> secim` without checking stream state; non-numeric
  input leaves the stream in a failed state and subsequent prompts fall through to defaults.
- In periodic mode, SHM and PCIe run **without** the ack handshake — a slow receiver gets the
  shared buffer overwritten under it. That is the intended real-time semantic ("the radar does
  not wait"), and it is what the receiver's gap detection is there to measure.
