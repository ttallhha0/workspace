cmake_minimum_required(VERSION 3.10)
project(TcpThreadOrnegi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# std::thread arka planda pthread kullanır; Threads::Threads bunu
# platforma göre (Linux/macOS/MinGW/MSVC) doğru şekilde bağlar.
find_package(Threads REQUIRED)

add_executable(tcp_sunucu src/sunucu.cpp)
target_include_directories(tcp_sunucu PRIVATE include)
target_link_libraries(tcp_sunucu PRIVATE Threads::Threads)

add_executable(tcp_istemci src/istemci.cpp)
target_include_directories(tcp_istemci PRIVATE include)
target_link_libraries(tcp_istemci PRIVATE Threads::Threads)

# Windows'ta soket fonksiyonları (socket/bind/connect/send/recv/...) ayrı bir
# sistem kütüphanesinde (ws2_32) yaşar; macOS/Linux'ta bunlar libc'nin parçası
# olduğu için ekstra bağlama gerekmez.
if(WIN32)
  target_link_libraries(tcp_sunucu PRIVATE ws2_32)
  target_link_libraries(tcp_istemci PRIVATE ws2_32)
endif()
