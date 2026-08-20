# TCP + Thread Örneği (C++)

Bu, `RadarIPC_System`'daki `TcpDriver.h` mantığının **minik ve izole** bir versiyonu.
Amaç: TCP soket programlama + `std::thread` mantığını, radar/CMake/AVX2 gibi başka
hiçbir şeyle karışmadan, tek başına kavramak.

İki küçük program var:

| Program | Ne yapar |
|---|---|
| `tcp_sunucu` | Bir portta dinler, gelen **her bağlantıyı kendi thread'inde** işler |
| `tcp_istemci` | Sunucuya **birden fazla bağlantıyı aynı anda** (paralel thread'lerle) açar |

Bu ikisi birlikte "neden sunucu thread kullanıyor?" sorusunun cevabını canlı olarak
gösteriyor: `tcp_istemci`, 4-5 bağlantıyı aynı anda açtığında, sunucu bunları
sırayla değil, **eş zamanlı** olarak cevaplayabiliyor.

## Windows'ta Sıfırdan Çalıştırma (Klonla → Derle → Çalıştır)

Bu kod hem macOS/Linux hem Windows'ta çalışır (bkz. [Windows'ta Çalışır mı?](#windowsta-çalışır-mı)
bölümü). Aşağıdaki adımlar, boş bir Windows makinesinde depoyu indirip 5 dakikada
çalıştırmanı sağlar.

### 1. Gerekli programları kur

| Program | Neden gerekli | İndirme |
|---|---|---|
| **Git for Windows** | Depoyu bilgisayarına indirmek (`git clone`) için | [git-scm.com/download/win](https://git-scm.com/download/win) |
| **CMake** | Projeyi derleme sistemine göre yapılandırmak için | [cmake.org/download](https://cmake.org/download) — kurulumda **"Add CMake to the system PATH"** seçeneğini işaretle |
| **Bir C++ derleyici** (aşağıdan birini seç) | Kodu derlemek için | — |

Derleyici için iki seçeneğin var — **hangisini seçersen seç, aşağıdaki adımlar aynı**:

- **Seçenek A — Visual Studio (en kolay)**: [Visual Studio Community](https://visualstudio.microsoft.com/downloads/)
  kur, kurulum ekranında **"Desktop development with C++"** iş yükünü işaretle.
  Bu, MSVC derleyicisini ve CMake entegrasyonunu otomatik kurar.
- **Seçenek B — MinGW-w64 (RadarIPC_System'ın kullandığı kurulum)**: [MSYS2](https://www.msys2.org/)
  kur, ardından **"MSYS2 MinGW64"** terminalini açıp şunu çalıştır:
  ```bash
  pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
  ```

### 2. Depoyu klonla

PowerShell veya Git Bash'te:

```powershell
git clone https://github.com/ttallhha0/workspace.git
cd workspace\tcp_thread_ornegi
```

### 3. Derle

**Visual Studio kullanıyorsan** (normal PowerShell'de):
```powershell
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
```
> Not: Visual Studio "multi-config" bir üretici olduğu için çıktı `build\Debug\` klasörüne gider —
> yani `.exe` dosyaları `build\Debug\tcp_sunucu.exe` yolunda olur.

**MSYS2/MinGW kullanıyorsan** ("MSYS2 MinGW64" terminalinde):
```bash
cmake -B build -G Ninja
cmake --build build
```
> Bu durumda çıktı doğrudan `build\` klasörüne gider: `build\tcp_sunucu.exe`.

### 4. Çalıştır

İki ayrı terminal aç (ikisi de aynı `tcp_thread_ornegi` klasöründe olmalı).

**Terminal 1 — sunucu** (Visual Studio ile derlediysen `build\Debug\...`, MinGW ile derlediysen `build\...`):
```powershell
.\build\Debug\tcp_sunucu.exe
```
veya
```powershell
.\build\tcp_sunucu.exe
```

**Terminal 2 — istemci** (5 paralel bağlantı):
```powershell
.\build\Debug\tcp_istemci.exe 5
```
veya
```powershell
.\build\tcp_istemci.exe 5
```

Çalışma mantığı ve beklenen çıktı, aşağıdaki [Çalıştırma](#çalıştırma) bölümünde
macOS için gösterilenle **tamamen aynı** — sadece komut satırında `.exe` uzantısı var.

### Windows'a özel küçük notlar

- **"Windows Defender Güvenlik Duvarı" uyarısı**: `tcp_sunucu.exe`'yi ilk çalıştırdığında
  Windows bir izin penceresi açabilir ("bu uygulamanın ağ erişimine izin ver mi?").
  **"İzin ver"** de — sunucu sadece `localhost` (kendi bilgisayarın) üzerinden dinlediği
  için bu tamamen güvenli.
- **"Bind basarisiz. Port 54000 kullaniliyor" hatası**: Bir önceki `tcp_sunucu.exe`
  hâlâ arka planda çalışıyor olabilir. Görev Yöneticisi'nden (`Ctrl+Shift+Esc`) kapat
  veya farklı bir terminalde `Ctrl+C` ile durdur.
- **`cmake` komutu tanınmıyor hatası**: CMake kurulumunda PATH seçeneğini işaretlemediysen,
  terminali kapatıp yeniden açman ya da CMake'i yeniden (PATH seçeneğiyle) kurman gerekir.

## Mantık — Adım Adım

### Sunucu tarafı (`src/sunucu.cpp`)

```
socket()  ->  bind()  ->  listen()  ->  [ döngü: accept() -> yeni thread ]
```

1. **`socket()`** — bir TCP soketi (bir "iletişim ucu") oluşturur. Henüz hiçbir
   şeye bağlı değildir, sadece bir tanıtıcı (file descriptor) döner.
2. **`bind()`** — bu soketi belirli bir port numarasına (`54000`) "sabitler".
   Böylece işletim sistemi "bu porta gelen her şeyi bu sokete ver" bilir.
3. **`listen()`** — soketi "bağlantı kabul etmeye hazır" moduna alır. `16`
   parametresi, henüz `accept()` edilmemiş bekleyen bağlantı kuyruğunun boyutu.
4. **`accept()`** — **bloklayıcı** bir çağrıdır: yeni bir istemci bağlanana kadar
   burada bekler. Bağlantı geldiğinde, o istemciye özel **yeni bir soket** döner
   (dinleme soketinden ayrı!).
5. Her `accept()` sonrası, o istemciyi işlemek için **yeni bir `std::thread`**
   açılıp `detach()` edilir — ana döngü hemen bir sonraki `accept()`'e döner.
   Thread açmasaydık, bir istemciyi işlerken (`recv`/`send` beklerken) yeni
   istemcileri kabul edemezdik; sunucu "tek seferde tek müşteri" olurdu.

```cpp
std::thread(istemciyiIsle, istemciSoket, istemciSayaci).detach();
```

- **`detach()`**: Thread kendi başına yaşar, işi bitince (fonksiyonu tamamlayınca)
  kaynaklarını kendisi serbest bırakır. Ana thread onu beklemez.
- Alternatifi **`join()`** olurdu, ama `join()` çağıran thread'i o thread bitene
  kadar bloklar — bu da sunucunun aynı anda tek istemci işlemesine geri dönmek
  olurdu. Bu yüzden sunucu tarafında `detach()`, istemci tarafında (aşağıda) `join()`
  kullanılıyor; ikisinin de ne zaman anlamlı olduğunu görebilmen için bilerek
  farklı seçildi.

### İstemci tarafı (`src/istemci.cpp`)

```
main() -> N adet thread aç (her biri: socket() -> connect() -> send() -> recv())
       -> hepsini join() ile bekle
```

- Her istemci thread'i **kendi bağımsız soketini** açar, sunucuya `connect()`
  olur, bir mesaj gönderir (`send`), cevabı okur (`recv`), kapatır.
- `main()`, tüm thread'leri başlattıktan sonra `for (auto& t : threadler) t.join();`
  ile **hepsinin bitmesini bekler** — aksi halde program, thread'ler işini
  bitirmeden sona erebilirdi (ve o thread'ler "sahipsiz" kalıp programın erken
  kapanmasına/çökmesine yol açabilirdi).

### Neden `std::mutex`?

`std::cout` ile ekrana yazmak thread-safe **değildir** garanti edilmiş şekilde —
iki thread aynı anda yazarsa satırlar birbirine girebilir. Bu yüzden her ekrana
yazma işlemi öncesi bir `std::mutex` kilitleniyor:

```cpp
{
    std::lock_guard<std::mutex> kilit(ekranKilidi);
    std::cout << "...";
}  // kilit burada otomatik açılır (RAII)
```

`std::lock_guard`, kapsam (`{ }`) bittiğinde kilidi otomatik açar — C'deki
"kilitle, işini yap, unutma açmayı" desenindeki "unutma açmayı" kısmını
derleyiciye bırakır (RAII deseni). Aynı fikir `aktifBaglantiSayisi` gibi paylaşılan
bir sayacı güncellerken de kullanılıyor — iki thread aynı anda `sayaç++` yapsa
sonuç yanlış çıkabilir (race condition), mutex bunu engeller.

## Derleme

```bash
cd tcp_thread_ornegi
cmake -B build
cmake --build build
```

Çıktılar: `build/tcp_sunucu` ve `build/tcp_istemci`.

## Windows'ta Çalışır mı?

**Evet — kod artık hem macOS/Linux hem Windows'ta aynı haliyle derlenir.**
İlk versiyonda sadece POSIX soketleri (`sys/socket.h`, `unistd.h`) kullanılıyordu;
bunlar Windows'ta **yok**, o yüzden orada derlenmezdi. Bunu çözmek için platform
farkını tek bir dosyada topladık:

```
include/soket_ortak.h   <-- Windows (Winsock2) / POSIX farkını gizleyen katman
src/sunucu.cpp          <-- artık hangi platformda olduğundan habersiz
src/istemci.cpp         <-- artık hangi platformda olduğundan habersiz
```

### Winsock ile POSIX arasındaki farklar (ve nasıl çözüldü)

| Konu | POSIX (macOS/Linux) | Windows (Winsock2) | Bu projede çözüm |
|---|---|---|---|
| Başlık dosyaları | `sys/socket.h`, `unistd.h`, `arpa/inet.h` | `winsock2.h`, `ws2tcpip.h` | `soket_ortak.h` içinde `#ifdef _WIN32` |
| Soket tipi | `int` | `SOCKET` (ayrı bir tip) | ortak `soket_t` takma adı |
| Geçersiz soket değeri | `-1` | `INVALID_SOCKET` | ortak `GECERSIZ_SOKET` sabiti |
| Soket kapatma | `close()` | `closesocket()` | ortak `soketKapat()` fonksiyonu |
| Kütüphaneyi başlatma | gerek yok | `WSAStartup()` çağrısı **zorunlu** | ortak `winsockBaslat()` (POSIX'te no-op) |
| Kütüphaneyi kapatma | gerek yok | `WSACleanup()` | ortak `winsockDurdur()` |
| Bağlanacak kütüphane | otomatik (libc) | `ws2_32.lib` | `CMakeLists.txt`'te `if(WIN32) ... ws2_32` |

`socket()`, `bind()`, `listen()`, `accept()`, `connect()`, `send()`, `recv()`,
`setsockopt()`, `sockaddr_in`, `htons()`, `inet_pton()`, `inet_ntop()` gibi asıl
"iş" yapan fonksiyonların **isimleri ve mantığı Windows'ta da birebir aynı** —
Winsock, tarihsel olarak zaten BSD soket API'sinin (POSIX'in temeli) üzerine
inşa edilmiş. Bu yüzden `sunucu.cpp`/`istemci.cpp` dosyalarının %95'i hiç
değişmedi; sadece platforma özel 6-7 küçük detay `soket_ortak.h`'a taşındı.
`RadarIPC_System`'daki `TcpDriver.h`'ın neden Windows'a özel yazıldığını
(WSAStartup, SOCKET tipi vs.) şimdi tanıyacaksın.

### Windows'ta derleme — adım adım komutlar

Depoyu klonlama, derleyici kurma ve tam derleme/çalıştırma komutları için yukarıdaki
[Windows'ta Sıfırdan Çalıştırma](#windowsta-sıfırdan-çalıştırma-klonla--derle--çalıştır)
bölümüne bak. Kısaca: CMake, `if(WIN32)` bloğu sayesinde `ws2_32` kütüphanesini hem
MSVC hem MinGW için otomatik bağlar — elle bir şey eklemen gerekmez.

> **MinGW notu:** `std::thread`/`std::mutex` kullanabilmek için MinGW-w64'ün
> "posix" thread modeliyle (veya güncel bir "win32" thread modeli sürümüyle —
> GCC 9+ bunu da destekliyor) kurulmuş olması gerekir. MSYS2 üzerinden kurulan
> güncel `mingw-w64-x86_64-gcc` paketleri bu konuda sorunsuzdur. Eski/özel bir
> MinGW dağıtımıyla "`thread` constructor not implemented" gibi bir hata alırsan,
> bu thread modeli meselesidir — derleyici sürümünü/kurulumunu değiştirmen gerekir.

### macOS'ta doğrulandı

Bu projeyi macOS'ta gerçekten derleyip çalıştırdım (`cmake -B build && cmake --build build`,
sonra sunucu + 5 paralel istemci) — POSIX dalı çalışıyor. Windows dalı, Winsock
API'sinin resmi dokümantasyonuna ve `RadarIPC_System`'ın kendi `TcpDriver.h`'ında
kullandığı aynı çağrılara göre yazıldı, ama bu makinede Windows olmadığı için
orada derlemeyi bizzat çalıştıramadım — MinGW veya MSVC'nin kurulu olduğu bir
Windows makinede yukarıdaki komutlarla derleyip deneyebilirsin.

## Çalıştırma

**Terminal 1** — sunucuyu başlat, açık bırak:

```bash
./build/tcp_sunucu
```

```
TCP sunucu 54000 portunda dinliyor... (durdurmak icin Ctrl+C)
```

**Terminal 2** — istemciyi çalıştır (parametre = kaç paralel bağlantı, varsayılan 4):

```bash
./build/tcp_istemci 5
```

Beklenen çıktı (istemci tarafı, sıralama thread zamanlamasına göre değişebilir):

```
5 istemci ayni anda sunucuya baglanacak...
Istemci-2 sunucudan cevap aldi: "ALINDI: Istemci-2 diyor: Merhaba radar!"
Istemci-3 sunucudan cevap aldi: "ALINDI: Istemci-3 diyor: Merhaba radar!"
Istemci-1 sunucudan cevap aldi: "ALINDI: Istemci-1 diyor: Merhaba radar!"
Istemci-4 sunucudan cevap aldi: "ALINDI: Istemci-4 diyor: Merhaba radar!"
Istemci-5 sunucudan cevap aldi: "ALINDI: Istemci-5 diyor: Merhaba radar!"
Tum istemciler tamamlandi.
```

Sunucu tarafında (Terminal 1) her bağlantı için ayrı bir satır ve **farklı bir
thread ID** göreceksin — bu, her istemcinin gerçekten kendi thread'inde işlendiğinin
kanıtı. `tcp_istemci`'yi tekrar tekrar (farklı sayılarla) çalıştırabilirsin, sunucuyu
kapatıp açmana gerek yok.

Sunucuyu durdurmak için Terminal 1'de `Ctrl+C`.

## Sırayı Göster: Thread'siz Olsaydı Ne Olurdu?

`main()` içindeki şu satırı:

```cpp
std::thread(istemciyiIsle, istemciSoket, istemciSayaci).detach();
```

geçici olarak şuna çevirip dene:

```cpp
istemciyiIsle(istemciSoket, istemciSayaci);  // thread YOK, doğrudan çağrı
```

Bu değişiklikle `tcp_istemci 5` çalıştırdığında sunucu, birinci istemcinin
bağlantısını **tamamen kapatana kadar** ikinciyi kabul edemez hale gelir (çünkü
`istemciyiIsle` içindeki `while` döngüsü `accept()`'e dönmeyi bloklar) — bu örnekte
istemciler mesaj gönderip hemen ayrıldığı için fark küçük olur, ama gerçek dünyada
(uzun süren bağlantılar, yavaş istemciler) bu, sunucuyu tek bir yavaş istemci
yüzünden tamamen kilitler. `RadarIPC_System`'daki `RadarAnteni`/`KomutaMerkezi` sürekli
açık kalan tek bir bağlantı kullandığı için bu sorunu yaşamaz, ama **çoklu istemci**
kabul eden herhangi bir TCP sunucusu (web sunucuları dahil) tam olarak bu yüzden
thread (veya benzeri bir eşzamanlılık modeli — event loop, thread pool vs.) kullanır.

## Genişletme Fikirleri

- **Büyük veri gönder**: `mesaj` yerine 10MB'lık bir buffer gönder, `TcpDriver.h`'daki
  gibi `send`/`recv`'i döngüyle (parça parça) çağırmak zorunda kalacaksın — TCP'nin
  neden "byte akışı" olduğunu elle deneyimlersin.
- **Gecikme ölç**: `std::chrono::steady_clock` ile gönderim/alım arasındaki süreyi
  ölç — `benchmark_sonuclari.csv`'deki `gecikme_ms` sütununun nasıl üretildiğini
  bu küçük ölçekte tekrar üret.
- **Paylaşılan bir toplam sayaç ekle**: Tüm istemcilerden gelen toplam byte sayısını
  tek bir `mutex`'li değişkende topla, sunucu kapanınca ekrana bas.
