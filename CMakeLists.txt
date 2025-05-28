cmake_minimum_required(VERSION 3.10)
project(Client)

# Устанавливаем стандарт C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Указываем исходный файл клиента
add_executable(client messengerclient.cpp)

# Для Windows подключаем библиотеку ws2_32
if(WIN32)
    target_link_libraries(client ws2_32)
endif()