# Son Bulgular — Dosya Yazımı Analizi

Bu belge, projedeki tüm dosya yazma (file I/O) noktalarının kapsamlı bir taramasının sonuçlarını içerir. İki bölüm halindedir:

1. **Correctness / veri bütünlüğü** iyileştirmeleri
2. **Performans / büyük veri** iyileştirmeleri

---

## 1. Taranan dosya yazma noktaları

Projede yazma yapan gerçek CSV dosyaları:

| Dosya | Yazan konum | Mod |
|---|---|---|
| `benchmark_sonuclari.csv` | `apps/KomutaMerkezi/main.cpp` (periyodik) | `ios::app` |
| `benchmark_toplu.csv` | `apps/KomutaMerkezi/main.cpp` (toplu bench) | `ios::app` |
| `benchmark_matris.csv` | `apps/Benchmark/main.cpp` | truncate |
| `kacak_uretici.csv` | `include/RadarIPC/KacakRaporlayici.h` | `ios::app` |
| `kacak_alici.csv` | `include/RadarIPC/KacakRaporlayici.h` | `ios::app` |

Ek olarak `ModKontrolKanali.h` named-pipe / FIFO kullanır; `SharedMemoryDriver` ve `PcieDriver` `ftruncate` ile shared-memory segmentini boyutlandırır — bunlar klasik "dosya yazımı" değildir, kapsam dışıdır.

---

## 2. Correctness / Veri Bütünlüğü Bulguları

### 🔴 Yüksek öncelikli

#### C1. Yazma hataları hiçbir yerde kontrol edilmiyor
- Konum: KomutaMerkezi:203-207, KacakRaporlayici:49, Benchmark:154
- Sorun: Disk dolu / permission denied durumunda `csv << ...` sessizce başarısız olur.
- Düzeltme: `open` sonrası `is_open()` kontrolü; koşu sonunda `if (!csv) std::cerr << "CSV yazim hatasi"`.

#### C2. Locale, ondalık ayracını bozabilir → CSV kırılır
- `LANG=tr_TR.UTF-8` altında `1.234` bazen `1,234` yazılır → CSV virgülle ayrıldığı için parse edilemez.
- Düzeltme: Her CSV `open`'ından sonra `csv.imbue(std::locale::classic());`

#### C3. `KacakRaporlayici` — önceki koşu düzgün kapanmadıysa header yapışır
- Konum: KacakRaporlayici:45-50
- Sorun: Önceki koşu `\n` ile bitmediyse yeni header eski satırın sonuna yapışır.
- Düzeltme: Append öncesi son karakter kontrolü; gerekirse önce `\n` bas.

#### C4. Aynı anda iki KomutaMerkezi → header iki kez yazılır (TOCTOU)
- Konum: KomutaMerkezi:171-185
- Sorun: İki paralel koşu "dosya boş" görüp ikisi de header yazar.
- Düzeltme: POSIX `flock(fd, LOCK_EX)` / Windows `LockFileEx` ile header check + write bloğunu atomik hale getir.

#### C5. CSV escaping yok — değer içinde virgül olursa dosya bozulur
- Konum: KomutaMerkezi:305-307 (`yorum.kimlik`), KacakRaporlayici:113 (`sebep`)
- Şu an kontrollü değerler kullanılıyor ama gelecekte "DOST, IFF-2" gibi bir değer CSV'yi çökertir.
- Düzeltme: RFC 4180 escape helper (`csvKacir`): `,`, `"`, `\n` varsa `"..."` ile sar, içteki `"` → `""`.

#### C6. Ondalık hassasiyet varsayılan → mikrosaniye ölçümü bilimsel notasyona döner
- Varsayılan `ofstream` precision = 6 anlamlı hane → `~0.0012345 ms` = `1.2345e-06` → bazı Excel'ler parse edemez.
- Düzeltme: `csv << std::fixed << std::setprecision(6);`

### 🟡 Düşük öncelikli

#### C7. `benchmark_matris.csv` truncate mod → önceki koşu kaybolur
- Konum: Benchmark:154 (`ios::app` yok)
- Düzeltme: Zaman-damgalı ad (`benchmark_matris_<tarih>.csv`) veya `--append` argümanı.

#### C8. Çıktı dizini konfigüre edilemez — çalışma dizinine yazılıyor
- Düzeltme: `Ortam::veriDizini()` helper, `RADAR_VERI_DIZINI` env vars'tan oku.

#### C9. `fsync`/`FlushFileBuffers` çağrılmıyor → güç kesintisinde son yazımlar gider
- Kritik benchmark özet dosyaları için koşu sonunda çağrılmalı.

#### C10. `KacakRaporlayici` koşuları ayırt edemez — sıralı koşular birbirine karışır
- Düzeltme: `run_id` sütunu veya konstruktörde opsiyonel run ID.

#### C11. `std::ifstream::peek()` yerine `std::filesystem::file_size()`
- Konum: KacakRaporlayici:45-48
- Daha doğru ve daha hızlı.

---

## 3. Performans / Büyük Veri Bulguları

### Kolay kazançlar (küçük değişiklik, büyük etki)

#### P1. Tampon boyutunu 1 MB'a çıkar — syscall sayısı 10-100x düşer
Varsayılan `ofstream` tamponu ~4 KB. Her `open`'dan **ÖNCE**:
```cpp
static char csvBuf[1024 * 1024];
csv.rdbuf()->pubsetbuf(csvBuf, sizeof(csvBuf));
csv.open("benchmark_matris.csv", std::ios::binary);
```

#### P2. `ios::binary` — Windows'ta LF→CRLF çevirisini kapat
Her satırda ek 1 bayt yazımı + karakter dönüştürme yok. Linux/macOS'ta zaten değişmez.

#### P3. Zaman-tabanlı flush aralığını uzat veya kaldır
- Konum: KomutaMerkezi:308-311 (`flushAraligi = 200ms`)
- 200ms → 5s yap veya tamamen kaldır (sadece kapanışta flush).
- Kaybolan: crash olursa son ~1MB veri. Benchmark koşularında kabul edilebilir.

### Yazım yolunun kendisini değiştir (3-10x hızlanma)

#### P4. `csv << a << "," << b << ...` yerine `snprintf` + tek `write()`
Her `operator<<` çağrısında sanal fonksiyon, locale kontrolü, iç mutex maliyeti var. Yerine:
```cpp
char satir[256];
int n = std::snprintf(satir, sizeof(satir),
    "%s,%d,%llu,%.6f,%.6f,%s,...\n",
    modKomutu.c_str(), boyutMB, (unsigned long long)no,
    gecikmeMs, throughputMBs, veriSaglam ? "SAGLAM" : "BOZUK", ...);
csv.write(satir, n);
```
Tipik olarak 3-5x daha hızlı, ve pahalı locale/precision ayarlarını atlar.

#### P5. `std::to_chars` (C++17) — locale-immun, `snprintf`'ten de hızlı
`snprintf` hâlâ `LC_NUMERIC` bakar; `to_chars` bakmaz, heap allocation da yapmaz.
```cpp
#include <charconv>
char buf[256], *p = buf;
auto [e1, _] = std::to_chars(p, buf + sizeof(buf), no);
*e1 = ','; p = e1 + 1;
auto [e2, _] = std::to_chars(p, buf + sizeof(buf), gecikmeMs, std::chars_format::fixed, 6);
// ...
csv.write(buf, p - buf);
```
`snprintf`'ten ~2x daha hızlı, hiç heap allocation yok.

#### P6. `std::ofstream` yerine `FILE*` + `setvbuf`
`streambuf` katmanı ek indirection getirir. Ham C API:
```cpp
FILE* fp = std::fopen("benchmark_sonuclari.csv", "ab");
static char buf[1024 * 1024];
std::setvbuf(fp, buf, _IOFBF, sizeof(buf));
std::fwrite(satir, 1, n, fp);
std::fflush(fp); std::fclose(fp);
```
`ofstream`'den tipik olarak %20-40 daha hızlı.

### Mimari değişiklikler (büyük veri için gerçek kazanç)

#### P7. Async writer thread — CSV yazımını hot path'ten çıkar
`KacakRaporlayici` zaten bu deseni kullanıyor. Aynısını KomutaMerkezi'nin hot-path CSV yazımına genelleştir:

- **Şu an**: işleyici thread her sinyalde CSV formatlıyor + yazıyor.
- **Sonra**: işleyici thread önceden formatlanmış satırı bir kuyruğa `push`; ayrı bir writer thread kuyruğu boşaltıp `fwrite`'lıyor.
- 100 MB sinyal işlerken diskin yavaşlığı işleyici döngüsünü **hiç yavaşlatmaz**.

`KacakRaporlayici`'yi generic `AsyncCsvYazar` sınıfına genelleştirmek en temiz refactoring.

#### P8. Batch yazımı — küçük yazımları büyük bloklar halinde birleştir
Async writer thread satır-satır yerine 100'lük batch'ler halinde tek `fwrite` yapsın. Kernel-user bariyer maliyeti düşer.

#### P9. `posix_fallocate` ile ön-tahsis
```cpp
posix_fallocate(fileno(fp), 0, 100 * 1024 * 1024);
```
Filesystem allocation gecikmesi kaybolur, dosya daha az fragment olur → sıralı yazım hızlanır. Fazla kısım koşu sonunda `ftruncate` ile kırpılır.

#### P10. `posix_fadvise` — kernel'e ipucu ver
```cpp
posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
posix_fadvise(fd, 0, yazilan, POSIX_FADV_DONTNEED);  // page cache'i şişirme
```
Büyük veri (>RAM) yazımında kritik — sistem geneli yavaşlamayı önler.

#### P11. Memory-mapped output (mmap) — en hızlı yazım yolu
Dosyayı belleğe eşle, `write()` yerine `memcpy` yap. Kernel arka planda diske yazar, syscall yok.
```cpp
int fd = open("out.csv", O_RDWR | O_CREAT, 0644);
size_t maxSize = 500 * 1024 * 1024;
ftruncate(fd, maxSize);
char* map = (char*)mmap(nullptr, maxSize, PROT_WRITE, MAP_SHARED, fd, 0);
// yazım = memcpy(map + offset, satir, n);
msync(map, kullanilan, MS_SYNC);
munmap(map, maxSize);
ftruncate(fd, kullanilan);
```
Sıralı sürekli yazımda `write()`'tan tipik olarak 1.5-3x daha hızlı.

#### P12. Binary format — CSV'yi tamamen bırak
Her CSV satırı ~150 bayt; sabit-boyutlu struct ~64 bayt:
```cpp
struct KayitV1 {
    uint32_t mod;
    uint32_t boyutMB;
    uint64_t sinyalNo;
    double gecikmeMs;
    double throughputMBs;
    uint8_t checksumDurumu;
    double kayipYuzdesi;
    uint64_t ulasmayanKumulatif;
    float mesafeKm;
    float aciDerece;
    uint8_t kimlik;
};
static_assert(sizeof(KayitV1) == 64);
```
- Dosya boyutu 2-3x küçülür → aynı sürede 2-3x daha çok kayıt yazılır.
- Yazım maliyeti minimum: format string yok, dönüşüm yok, sadece `fwrite`.
- Ayrı `bin2csv` yardımcı programı ile isteyenler CSV'ye dönüştürür.
- Bu, Perf/DTrace/LTTng gibi profesyonel benchmark araçlarının yaklaşımıdır.

#### P13. Streaming sıkıştırma (zstd)
- Multi-thread zstd seviye 1: CPU maliyeti ihmal edilebilir.
- Dosya boyutu 5-20x küçülür → disk yazım bant genişliği aynı oranda azalır.
- Async writer thread'in içine yerleştirilebilir: `.csv.zst` dosyası, `zstdcat` ile okunur.

#### P14. `O_DIRECT` — page cache'i tamamen atla (>10 GB senaryosu)
- Yazma tamponları 512/4096-byte hizalı olmalı; `posix_memalign` gerekir.
- Karmaşık, ama sürekli 100 GB üzerine yazan senaryolarda kaçınılmaz.

---

## 4. Öncelik Sırası (Uygulama Yol Haritası)

| # | Değişiklik | Emek | Beklenen kazanç |
|---|---|---|---|
| 1 | Tampon boyutunu 1 MB'a çıkar (P1) | 5 dk | Yüksek |
| 2 | Flush aralığını uzat veya kaldır (P3) | 2 dk | Orta |
| 3 | `to_chars` + tek `write` (P5) | 30 dk | Yüksek |
| 4 | `FILE*` + `setvbuf` (P6) | 30 dk | Orta |
| 5 | Async writer thread (P7) | 2-3 saat | **Çok yüksek** — hot-path'i diskten ayırır |
| 6 | `posix_fallocate` + `fadvise` (P9, P10) | 20 dk | Yüksek (büyük dosya) |
| 7 | mmap tabanlı yazım (P11) | 4-6 saat | Çok yüksek |
| 8 | Binary format + `bin2csv` (P12) | 1 gün | **En yüksek** — 2-3x hem yazım hem depolama |
| 9 | zstd async sıkıştırma (P13) | 4 saat | Depolamada 5-20x |

**Önerilen ilk adım paketi:** 1, 2, 3, 5 — yarım gün, hot-path CSV yazımı işleyici thread'i artık hiç engellemez.

**Uzun vadeli hedef:** Async writer thread (P7) altyapısı üzerine binary format (P12) + streaming zstd (P13). Bu üçlü, projenin veri toplama katmanını profesyonel benchmark araçları seviyesine çıkarır.

---

*Bu belge, projenin `apps/` ve `include/RadarIPC/` altındaki tüm C++ kaynaklarının incelenmesiyle üretilmiştir.*
