// tcp_sunucu — bir TCP portunda dinler, gelen HER bağlantıyı KENDİ thread'inde
// işler. Böylece 5 istemci aynı anda bağlansa bile birbirini bloklamaz.
//
// Akış: socket() -> bind() -> listen() -> [döngü: accept() -> yeni thread]
//
// Windows (Winsock2) ve POSIX (macOS/Linux) arasındaki isim/tip farkları
// "soket_ortak.h" içinde soyutlandı — bu dosya platformdan habersizdir,
// hem macOS/Linux'ta hem Windows'ta (MinGW veya MSVC) aynen derlenir.

#include "soket_ortak.h"

#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {
constexpr int PORT = 54000;
constexpr int TAMPON_BOYU = 1024;

// std::cout aynı anda birden fazla thread'den çağrılırsa çıktı karışabilir
// (satırlar birbirine girer). Bu yüzden ekrana yazmadan önce bu kilidi alıyoruz.
std::mutex ekranKilidi;

std::mutex sayacKilidi;
int aktifBaglantiSayisi = 0;
}  // namespace

// Her bağlantı kabul edildiğinde bu fonksiyon YENİ bir thread üzerinde
// çalışır (bkz. main() içindeki std::thread(...).detach() satırı).
void istemciyiIsle(soket_t istemciSoket, int istemciId) {
  {
    std::lock_guard<std::mutex> kilit(sayacKilidi);
    aktifBaglantiSayisi++;
  }

  char tampon[TAMPON_BOYU];

  // TCP bir "byte akışı"dır, mesaj sınırı yoktur — recv() bir çağrıda
  // istemcinin gönderdiği HER şeyi vermeyebilir. Burada basitlik için
  // "her recv() bir mesajdır" kabul ediyoruz; gerçek protokollerde
  // (örn. RadarIPC'deki TcpDriver) veri boyutu önceden bilinip döngüyle okunur.
  while (true) {
    std::memset(tampon, 0, TAMPON_BOYU);
    int alinanBayt = static_cast<int>(recv(istemciSoket, tampon, TAMPON_BOYU - 1, 0));

    if (alinanBayt <= 0) {
      // 0  -> istemci bağlantıyı düzgünce kapattı (close/disconnect)
      // <0 -> okuma hatası
      break;
    }

    {
      std::lock_guard<std::mutex> kilit(ekranKilidi);
      std::cout << "[thread " << std::this_thread::get_id() << "] "
                << "Istemci-" << istemciId << " -> \"" << tampon << "\"\n";
    }

    std::string cevap = "ALINDI: " + std::string(tampon);
    send(istemciSoket, cevap.c_str(), static_cast<int>(cevap.size()), 0);
  }

  soketKapat(istemciSoket);

  {
    std::lock_guard<std::mutex> kilit(sayacKilidi);
    aktifBaglantiSayisi--;
  }
  {
    std::lock_guard<std::mutex> kilit(ekranKilidi);
    std::cout << "Istemci-" << istemciId << " ayrildi. Aktif baglanti sayisi: "
              << aktifBaglantiSayisi << "\n";
  }
}

int main() {
  if (!winsockBaslat()) {
    std::cerr << "Winsock baslatilamadi.\n";
    return 1;
  }

  soket_t sunucuSoket = socket(AF_INET, SOCK_STREAM, 0);
  if (sunucuSoket == GECERSIZ_SOKET) {
    std::cerr << "Soket olusturulamadi.\n";
    return 1;
  }

  // Sunucuyu kapatip hemen yeniden başlattığında "Address already in use"
  // hatası almamak için bu seçenek açılır. setsockopt'un optval parametresi
  // Windows'ta "const char*", POSIX'te "const void*" ister — bu yüzden cast var.
  int secenek = 1;
  setsockopt(sunucuSoket, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&secenek), sizeof(secenek));

  sockaddr_in adres{};
  adres.sin_family = AF_INET;
  adres.sin_addr.s_addr = INADDR_ANY;  // her ağ arayüzünden bağlantı kabul et
  adres.sin_port = htons(PORT);        // host byte order -> network byte order

  if (bind(sunucuSoket, reinterpret_cast<sockaddr*>(&adres), sizeof(adres)) != 0) {
    std::cerr << "bind() basarisiz. Port " << PORT << " baska bir islem tarafindan "
                 "kullaniliyor olabilir.\n";
    return 1;
  }

  if (listen(sunucuSoket, 16) != 0) {
    std::cerr << "listen() basarisiz.\n";
    return 1;
  }

  std::cout << "TCP sunucu " << PORT << " portunda dinliyor... (durdurmak icin Ctrl+C)\n";

  int istemciSayaci = 0;

  while (true) {
    sockaddr_in istemciAdres{};
    socklen_t istemciAdresBoyu = sizeof(istemciAdres);

    // accept() BLOKLAYICI bir çağrıdır: yeni bir istemci bağlanana kadar
    // burada bekler. Thread kullanmasaydık, bir istemciyi işlerken yeni
    // istemcileri kabul edemezdik.
    soket_t istemciSoket = accept(sunucuSoket, reinterpret_cast<sockaddr*>(&istemciAdres),
                                   &istemciAdresBoyu);
    if (istemciSoket == GECERSIZ_SOKET) {
      continue;
    }

    istemciSayaci++;
    char ipMetni[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &istemciAdres.sin_addr, ipMetni, INET_ADDRSTRLEN);

    {
      std::lock_guard<std::mutex> kilit(ekranKilidi);
      std::cout << "Yeni baglanti: Istemci-" << istemciSayaci << " (" << ipMetni << ")\n";
    }

    // Bu istemciyi ANA thread'i bloklamadan işlemek için yeni bir thread açıyoruz.
    // detach(): thread kendi başına çalışıp bitince kaynaklarını kendisi serbest
    // bırakır; ana thread onu join() ile beklemek zorunda kalmaz ve hemen
    // accept()'e geri dönüp bir sonraki istemciyi kabul edebilir.
    std::thread(istemciyiIsle, istemciSoket, istemciSayaci).detach();
  }

  soketKapat(sunucuSoket);
  winsockDurdur();
  return 0;
}
