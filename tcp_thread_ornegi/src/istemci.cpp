// tcp_istemci — sunucuya N adet PARALEL bağlantı açar (her biri kendi thread'inde),
// bir mesaj gönderir, cevabı bekler, ekrana basar.
//
// Kullanim: ./tcp_istemci [istemci_sayisi]   (varsayilan: 4)
//
// Bu, sunucudaki "her bağlantı = ayrı thread" modelinin NEDEN gerekli olduğunu
// gösterir: tüm bu istemciler sunucuya aynı anda bağlanır; sunucu thread
// kullanmasaydı bunları sırayla (biri bitene kadar diğeri bekleyerek) işlerdi.
//
// Windows/POSIX farkı "soket_ortak.h" içinde soyutlandı, bu dosya platformdan
// habersizdir.

#include "soket_ortak.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int PORT = 54000;
constexpr int TAMPON_BOYU = 1024;
constexpr const char* SUNUCU_IP = "127.0.0.1";

std::mutex ekranKilidi;
}  // namespace

// Her istemci thread'i bu fonksiyonu çalıştırır: kendi soketini açar,
// bağlanır, mesaj gönderir, cevabı okur, kapatır.
void baglanVeMesajGonder(int istemciId) {
  soket_t soket = socket(AF_INET, SOCK_STREAM, 0);
  if (soket == GECERSIZ_SOKET) {
    std::lock_guard<std::mutex> kilit(ekranKilidi);
    std::cerr << "Istemci-" << istemciId << ": soket olusturulamadi.\n";
    return;
  }

  sockaddr_in sunucuAdres{};
  sunucuAdres.sin_family = AF_INET;
  sunucuAdres.sin_port = htons(PORT);
  inet_pton(AF_INET, SUNUCU_IP, &sunucuAdres.sin_addr);

  if (connect(soket, reinterpret_cast<sockaddr*>(&sunucuAdres), sizeof(sunucuAdres)) != 0) {
    std::lock_guard<std::mutex> kilit(ekranKilidi);
    std::cerr << "Istemci-" << istemciId << ": sunucuya baglanilamadi. "
                 "Once 'tcp_sunucu' calisiyor mu diye kontrol et.\n";
    soketKapat(soket);
    return;
  }

  std::string mesaj = "Istemci-" + std::to_string(istemciId) + " diyor: Merhaba radar!";
  send(soket, mesaj.c_str(), static_cast<int>(mesaj.size()), 0);

  char tampon[TAMPON_BOYU];
  std::memset(tampon, 0, TAMPON_BOYU);
  int alinan = static_cast<int>(recv(soket, tampon, TAMPON_BOYU - 1, 0));

  {
    std::lock_guard<std::mutex> kilit(ekranKilidi);
    if (alinan > 0) {
      std::cout << "Istemci-" << istemciId << " sunucudan cevap aldi: \"" << tampon << "\"\n";
    } else {
      std::cout << "Istemci-" << istemciId << ": cevap alinamadi.\n";
    }
  }

  soketKapat(soket);
}

int main(int argc, char* argv[]) {
  if (!winsockBaslat()) {
    std::cerr << "Winsock baslatilamadi.\n";
    return 1;
  }

  int istemciSayisi = 4;
  if (argc > 1) {
    istemciSayisi = std::atoi(argv[1]);
    if (istemciSayisi <= 0) istemciSayisi = 4;
  }

  std::cout << istemciSayisi << " istemci ayni anda sunucuya baglanacak...\n";

  std::vector<std::thread> threadler;
  threadler.reserve(istemciSayisi);
  for (int i = 1; i <= istemciSayisi; i++) {
    threadler.emplace_back(baglanVeMesajGonder, i);
  }

  // join(): ana thread, listedeki HER thread bitene kadar burada bekler.
  // (detach() kullansaydık main() thread'ler bitmeden de sona erebilirdi.)
  for (auto& t : threadler) {
    t.join();
  }

  std::cout << "Tum istemciler tamamlandi.\n";
  winsockDurdur();
  return 0;
}
