// soket_ortak.h — Windows (Winsock2) ile POSIX (macOS/Linux) soket API'leri
// arasındaki isim/tip farklarını tek yerde toplayan ince bir uyumluluk katmanı.
//
// Fikir: sunucu.cpp / istemci.cpp bu dosyayı include ettikten sonra
// hangi platformda derlendiklerinden HABERSİZ kalırlar — hep aynı
// soket_t / GECERSIZ_SOKET / soketKapat() / winsockBaslat() isimlerini kullanırlar.
//
// RadarIPC_System'daki TcpDriver.h de tam olarak bu ihtiyaç yüzünden Windows'a
// özel yazılmıştı; burada aynı sorunu iki platformu da desteklecek şekilde çözüyoruz.

#pragma once

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

using soket_t = SOCKET;
constexpr soket_t GECERSIZ_SOKET = INVALID_SOCKET;

inline void soketKapat(soket_t s) { closesocket(s); }

// Windows'ta soket kullanmadan önce Winsock kütüphanesinin "ayağa kalkması"
// gerekir (WSAStartup) — POSIX'te böyle bir adım yoktur, işletim sistemi
// soketleri baştan destekler.
inline bool winsockBaslat() {
  WSADATA wsaData;
  return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

inline void winsockDurdur() { WSACleanup(); }

#else  // POSIX (macOS / Linux)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using soket_t = int;
constexpr soket_t GECERSIZ_SOKET = -1;

inline void soketKapat(soket_t s) { close(s); }
inline bool winsockBaslat() { return true; }  // POSIX'te yapılacak bir şey yok
inline void winsockDurdur() {}

#endif
