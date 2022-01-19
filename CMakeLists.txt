cmake_minimum_required(VERSION 3.20)
set(MY_PROJECT_NAME "beast-issue-2380")
project("${MY_PROJECT_NAME}")

set(CMAKE_CXX_STANDARD 20)

find_package(Boost 1.79 REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(beast-tls main.cpp)
add_executable(asio-tls main.cpp)
add_executable(beast-tcp main.cpp)
add_executable(asio-tcp main.cpp)

set(LIBS Boost::boost Boost::system Boost::thread
        OpenSSL::SSL OpenSSL::Crypto
        Threads::Threads)

target_link_libraries(beast-tls PUBLIC ${LIBS})
target_link_libraries(asio-tls PUBLIC ${LIBS})
target_link_libraries(beast-tcp PUBLIC ${LIBS})
target_link_libraries(asio-tcp PUBLIC ${LIBS})

target_compile_definitions(asio-tls PUBLIC ASIO_STREAMS=1)
target_compile_definitions(beast-tcp PUBLIC WITHOUT_TLS=1)
target_compile_definitions(asio-tcp PUBLIC ASIO_STREAMS=1 WITHOUT_TLS=1)

