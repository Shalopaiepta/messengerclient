#include <iostream>
#include <string>
#include <thread>
#include <sstream>
#include <chrono>
#include <atomic>    // std::atomic_bool
#include <mutex>     // std::mutex
#include <algorithm> // std::remove, std::transform
#include <vector>    // std::vector
#include <cctype>    // std::toupper
#include <iomanip>   // std::put_time
#include <map>       // std::map (для будущих непрочитанных)

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h> 
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h> 
#include <string.h> 
#include <sys/ioctl.h> 
#include <sys/select.h> 
#endif

// Кросс-платформенные определения
#ifdef _WIN32
typedef SOCKET SocketType;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define SOCKET_ERROR_VALUE SOCKET_ERROR
#define CLOSE_SOCKET closesocket
#define GET_LAST_ERROR WSAGetLastError()
#else
typedef int SocketType;
#define INVALID_SOCKET_VALUE -1
#define SOCKET_ERROR_VALUE -1
#define CLOSE_SOCKET close
#define GET_LAST_ERROR errno
#endif

// Глобальные переменные состояния клиента
SocketType G_clientSocket = INVALID_SOCKET_VALUE;
std::atomic<bool> G_clientRunning(true);            // Управляет основным циклом клиента и потоком приемника
std::atomic<bool> G_programShouldExit(false);       // Флаг для полного завершения программы
std::mutex G_coutMutex;                             // Защита для std::cout
std::atomic<bool> G_loggedIn(false);                // Статус логина
std::string G_currentUsername;                      // Имя текущего пользователя
std::atomic<bool> G_inChatMode(false);              // Флаг: активен личный чат
std::string G_currentChatPartner;                   // Имя собеседника в личном чате
std::atomic<bool> G_inGroupChatMode(false);         // Флаг: активен групповой чат
std::string G_currentGroupName;                     // Имя текущей группы
std::atomic<bool> G_waitingForChatInitiation(false);// Флаг: ожидается ответ сервера на открытие чата (история)
std::atomic<bool> G_isReceivingFriendList(false);   // Флаг: идет прием списка друзей
std::atomic<bool> G_isReceivingGroupList(false);    // Флаг: идет прием списка групп


// --- Прототипы функций UI ---
void clearConsoleScreen();
void printWelcomeMessage();
void printHelp(bool isLoggedIn, bool isInChatMode, bool isInGroupChatMode, const std::string& currentChatTarget);
void displayPrompt();
void printInitialScreen();
void displayChatMessageClient(const std::string& timestamp_str, const std::string& sender, const std::string& message_text);
std::string formatTimestampForDisplay(const std::string& full_timestamp_from_server);
std::string getCurrentLocalTimestampForChatDisplay();
// --- Конец прототипов UI ---


void clearConsoleScreen() {
#ifdef _WIN32
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    DWORD cellCount;
    COORD homeCoords = { 0, 0 };

    if (hStdOut == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleScreenBufferInfo(hStdOut, &csbi)) return;
    cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    if (!FillConsoleOutputCharacter(hStdOut, (TCHAR)' ', cellCount, homeCoords, &count)) return;
    if (!FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, cellCount, homeCoords, &count)) return;
    SetConsoleCursorPosition(hStdOut, homeCoords);
#else
    // ANSI-последовательность для очистки экрана и перемещения курсора в начало
    std::cout << "\033[2J\033[1;1H" << std::flush;
#endif
}

void printWelcomeMessage() {
    std::cout << R"(
================================================================================
 Добро пожаловать в наш Консольный Мессенджер!
================================================================================

        /\_/\           D I N O G R A M
       ( o.o )            messenger
        > ^ <
       /  |  \
      /   |   \
     /    |    \
    (-----|-----)
     WWWWWWWWWWW
      WWWWWWWWW
       WWWWWWW

     Просто. Надежно. Консольно.

Для начала работы, пожалуйста, войдите в систему или зарегистрируйтесь.
)" << std::endl;
}

void printHelp(bool isLoggedIn, bool isInChatMode, bool isInGroupChatMode, const std::string& currentChatTarget) {
    std::cout << "\n--- Доступные команды ---\n";
    if (isInGroupChatMode) {
        std::cout << "  Вы находитесь в групповом чате '" << currentChatTarget << "'.\n";
        std::cout << "  Просто вводите текст и нажимайте Enter для отправки сообщения.\n";
        std::cout << "  /exit_chat - Покинуть текущий чат.\n";
    }
    else if (isInChatMode) {
        std::cout << "  Вы находитесь в чате с " << currentChatTarget << ".\n";
        std::cout << "  Просто вводите текст и нажимайте Enter для отправки сообщения.\n";
        std::cout << "  /exit_chat - Покинуть текущий чат.\n";
    }
    else if (!isLoggedIn) {
        std::cout << "  LOGIN <имя_пользователя> <пароль> - Войти в систему\n";
        std::cout << "  REGISTRATION <имя_пользователя> <пароль> - Зарегистрировать нового пользователя\n";
        std::cout << "  HELP - Показать это сообщение помощи\n";
        std::cout << "  EXIT - Выйти из программы\n";
    }
    else { // Залогинен, не в чате
        std::cout << "  CREATE_GROUP <название_группы> - Создать новую группу.\n";
        std::cout << "  JOIN_GROUP <название_группы> - Присоединиться к существующей группе.\n";
        std::cout << "  GROUPCHAT <название_группы> - Открыть групповой чат.\n";
        std::cout << "  LIST_MY_GROUPS - Показать список ваших групп.\n";
        std::cout << "  CHAT <имя_пользователя> - Открыть личный чат.\n";
        std::cout << "  FRIENDS - Показать список ваших личных чатов и их статус.\n"; // Сервер поддерживает GET_CHAT_PARTNERS
        std::cout << "  HELP - Показать это сообщение помощи\n";
        std::cout << "  EXIT - Выйти из текущей учетной записи (LOGOUT)\n";
    }
    std::cout << "-------------------------\n" << std::endl;
}


void displayPrompt() {
    // Очистка текущей строки перед выводом промпта (для красоты при асинхронных сообщениях)
    std::cout << "\r" << std::string(120, ' ') << "\r";
    if (G_inGroupChatMode.load()) {
        std::cout << "[" << G_currentUsername << " @ Group:" << G_currentGroupName << "] > " << std::flush;
    }
    else if (G_inChatMode.load()) {
        std::cout << "[" << G_currentUsername << " @ " << G_currentChatPartner << "] > " << std::flush;
    }
    else if (G_loggedIn.load()) {
        std::cout << "[" << G_currentUsername << "] > " << std::flush;
    }
    else {
        std::cout << "Messenger > " << std::flush;
    }
}

void printInitialScreen() {
    std::lock_guard<std::mutex> lock(G_coutMutex);
    clearConsoleScreen();
    printWelcomeMessage();
    std::string target = G_inGroupChatMode.load() ? G_currentGroupName : (G_inChatMode.load() ? G_currentChatPartner : "");
    printHelp(G_loggedIn.load(), G_inChatMode.load(), G_inGroupChatMode.load(), target);
    displayPrompt();
}

// Форматирует серверный timestamp (YYYY-MM-DD HH:MM:SS) для отображения.
// Если сегодня, то HH:MM, иначе [DD.MM | HH:MM]
std::string formatTimestampForDisplay(const std::string& full_timestamp_from_server) {
    // Проверка базового формата YYYY-MM-DD HH:MM:SS
    if (full_timestamp_from_server.length() == 19 &&
        full_timestamp_from_server[4] == '-' && full_timestamp_from_server[7] == '-' &&
        full_timestamp_from_server[10] == ' ' &&
        full_timestamp_from_server[13] == ':' && full_timestamp_from_server[16] == ':') {

        std::string month_str = full_timestamp_from_server.substr(5, 2);
        std::string day_str = full_timestamp_from_server.substr(8, 2);
        std::string hour_str = full_timestamp_from_server.substr(11, 2);
        std::string minute_str = full_timestamp_from_server.substr(14, 2);

        std::string formatted_date = day_str + "." + month_str;
        std::string formatted_time = hour_str + ":" + minute_str;

        // Получаем текущую дату для сравнения
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm buf_today;
#ifdef _WIN32
        localtime_s(&buf_today, &in_time_t);
#else
        localtime_r(&in_time_t, &buf_today); // Потокобезопасно
#endif
        std::stringstream today_ss;
        today_ss << std::setfill('0') << std::setw(2) << buf_today.tm_mday << "."
            << std::setfill('0') << std::setw(2) << (buf_today.tm_mon + 1);
        std::string today_date_str = today_ss.str();

        if (formatted_date == today_date_str) {
            return formatted_time; // Сообщение от сегодня - только время
        }
        else {
            return "[" + formatted_date + " | " + formatted_time + "]"; // Иначе - дата и время
        }
    }
    // Если пришел уже короткий формат HH:MM (например, от displayChatMessageClient для своих сообщений)
    if (full_timestamp_from_server.length() == 5 && full_timestamp_from_server[2] == ':') {
        return full_timestamp_from_server;
    }
    return full_timestamp_from_server; // Если формат неизвестен, вернуть как есть
}

// Возвращает текущее локальное время в формате HH:MM для отображения собственных сообщений
std::string getCurrentLocalTimestampForChatDisplay() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
#ifdef _WIN32
    localtime_s(&buf, &in_time_t);
#else
    localtime_r(&in_time_t, &buf);
#endif
    std::stringstream ss;
    ss << std::put_time(&buf, "%H:%M");
    return ss.str();
}

// Отображает сообщение чата в консоли
void displayChatMessageClient(const std::string& timestamp_str, const std::string& sender, const std::string& message_text) {
    std::string display_ts = formatTimestampForDisplay(timestamp_str);
    std::cout << display_ts << " ";
    if (sender == G_currentUsername) { // Свои сообщения
        std::cout << "Вы: ";
    }
    else { // Сообщения от других
        std::cout << sender << ": ";
    }
    std::cout << message_text << std::endl;
}


// Читает строку от сервера (до '\n')
std::string clientReadLine(SocketType socket) {
    std::string line;
    char ch;
    while (G_clientRunning.load()) { // Проверка флага для корректного завершения потока
        int bytesReceived = recv(socket, &ch, 1, 0);
        if (bytesReceived == 0) { /* Сервер закрыл соединение */ return ""; }
        if (bytesReceived < 0) {   /* Ошибка сокета */
#ifdef _WIN32 // Типичные ошибки разрыва/закрытия сокета
            if (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAESHUTDOWN || WSAGetLastError() == WSAENOTSOCK || WSAGetLastError() == WSAEINTR) return "";
#else
            if (errno == ECONNRESET || errno == EPIPE || errno == EBADF || errno == EINTR) return "";
#endif
            return ""; // Другая ошибка
        }
        if (ch == '\n') break;
        if (ch != '\r') line += ch; // Игнорируем '\r'
    }
    return line;
}

// Отправляет сообщение серверу, добавляя '\n'
void clientSendMessage(SocketType socket, const std::string& message) {
    if (socket == INVALID_SOCKET_VALUE || !G_clientRunning.load()) return;

    std::string cleanedMessage = message;
    // Убираем '\r' на всякий случай, если ввод содержит CRLF
    cleanedMessage.erase(std::remove(cleanedMessage.begin(), cleanedMessage.end(), '\r'), cleanedMessage.end());

    std::string msg_to_send = cleanedMessage + "\n";
    if (send(socket, msg_to_send.c_str(), static_cast<int>(msg_to_send.length()), 0) == SOCKET_ERROR_VALUE) {
        std::lock_guard<std::mutex> lock(G_coutMutex);
        std::cout << "\r" << std::string(120, ' ') << "\r"; // Очистка строки ввода
        std::cerr << "[СИСТЕМА] Ошибка отправки: " << GET_LAST_ERROR << ". Соединение может быть разорвано." << std::endl;
        displayPrompt();
    }
}

// Извлекает имя пользователя из приветственного сообщения сервера
std::string parseUsernameFromWelcome(const std::string& serverResponse) {
    std::string prefix1 = "OK_LOGIN Welcome, ";
    std::string prefix2 = "OK_REGISTERED Welcome, ";
    std::string suffix = "!";
    std::string username = "";
    size_t startPos = std::string::npos;

    if (serverResponse.rfind(prefix1, 0) == 0) { // rfind с 0 == starts_with
        startPos = prefix1.length();
    }
    else if (serverResponse.rfind(prefix2, 0) == 0) {
        startPos = prefix2.length();
    }

    if (startPos != std::string::npos) {
        size_t endPos = serverResponse.rfind(suffix); // Ищем '!' с конца
        if (endPos != std::string::npos && endPos > startPos) {
            username = serverResponse.substr(startPos, endPos - startPos);
        }
    }
    return username;
}


// Поток для приема сообщений от сервера
void receiveMessagesThreadFunc() {
    fd_set readSet;
    timeval timeout;
    bool chat_history_loading = false; // Флаг: идет ли загрузка истории чата
    std::string chat_target_loading;   // Для какого чата/группы грузится история

    while (G_clientRunning.load()) {
        if (G_programShouldExit.load()) break; // Полный выход из программы
        if (G_clientSocket == INVALID_SOCKET_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        FD_ZERO(&readSet);
        FD_SET(G_clientSocket, &readSet);
        timeout.tv_sec = 1; // Таймаут для select, чтобы поток не блокировался навечно
        timeout.tv_usec = 0;

        int selectNfds = G_clientSocket + 1; // Для Unix-like систем
#ifdef _WIN32
        selectNfds = 0; // Для Windows select nfds игнорируется
#endif
        int selectResult = select(selectNfds, &readSet, nullptr, nullptr, &timeout);

        if (G_programShouldExit.load()) break; // Перепроверка после select
        // Если клиент уже не должен работать (например, после LOGOUT), но программа еще не завершается
        if (!G_clientRunning.load() && !G_programShouldExit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }


        if (selectResult < 0) { // Ошибка select или сокет закрыт
            int error_code = GET_LAST_ERROR;
            if (G_clientRunning.load()) { // Если ошибка произошла во время активной работы
                std::lock_guard<std::mutex> lock(G_coutMutex);
                clearConsoleScreen();
                std::cerr << "\n[ПРИЕМНИК] select ошибка " << error_code << " или сокет закрыт." << std::endl;
                std::cout << "Нажмите Enter для выхода..." << std::flush;
            }
            // Сброс всех состояний
            G_loggedIn = false; G_currentUsername.clear(); G_inChatMode = false; G_currentChatPartner.clear();
            G_inGroupChatMode = false; G_currentGroupName.clear(); G_waitingForChatInitiation = false;
            G_isReceivingFriendList = false; G_isReceivingGroupList = false;
            if (G_clientSocket != INVALID_SOCKET_VALUE) { CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; }
            G_programShouldExit = true; // Инициируем полный выход
            G_clientRunning = false;    // Останавливаем этот поток и основной цикл ввода
            break;
        }

        if (selectResult > 0 && FD_ISSET(G_clientSocket, &readSet)) { // Есть данные для чтения
            std::string message = clientReadLine(G_clientSocket);
            std::lock_guard<std::mutex> lock(G_coutMutex); // Защищаем вывод в консоль

            // Если программа завершается и пришло пустое сообщение (например, из-за закрытия сокета) - выходим
            if (G_programShouldExit.load() && message.empty()) break;

            if (message.empty() && G_clientRunning.load()) { // Сервер отключился или ошибка чтения
                std::cout << "\r" << std::string(120, ' ') << "\r";
                std::cout << "[ПРИЕМНИК] Сервер отключился или ошибка чтения." << std::endl;
                // Сброс состояний, аналогично ошибке select
                G_loggedIn = false; G_currentUsername.clear(); G_inChatMode = false; G_currentChatPartner.clear();
                G_inGroupChatMode = false; G_currentGroupName.clear(); G_waitingForChatInitiation = false;
                G_isReceivingFriendList = false; G_isReceivingGroupList = false;
                if (G_clientSocket != INVALID_SOCKET_VALUE) { CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; }
                // Не ставим G_programShouldExit = true здесь, даем возможность переподключиться из main
                if (!G_inChatMode.load() && !G_inGroupChatMode.load()) displayPrompt(); // Обновить промпт, если не в чате
            }
            else if (!message.empty()) { // Получено непустое сообщение
                std::cout << "\r" << std::string(120, ' ') << "\r"; // Очистка строки ввода
                std::string prefix, payload;
                size_t space_pos = message.find(' ');
                if (space_pos != std::string::npos) {
                    prefix = message.substr(0, space_pos);
                    payload = message.substr(space_pos + 1);
                }
                else {
                    prefix = message; // Сообщение без аргументов
                }

                bool handled = false; // Флаг, что сообщение было обработано специфическим обработчиком

                // --- Обработка инициации личного чата (когда ждем HISTORY_START или NO_HISTORY) ---
                if (G_waitingForChatInitiation.load() && !G_inGroupChatMode.load() && !G_currentChatPartner.empty() && G_currentChatPartner == payload) {
                    if (prefix == "HISTORY_START") {
                        G_inChatMode = true; G_inGroupChatMode = false; G_waitingForChatInitiation = false;
                        chat_history_loading = true; chat_target_loading = payload; // Запоминаем для кого грузим историю
                        clearConsoleScreen();
                        std::cout << "--- Чат с " << G_currentChatPartner << " ---" << std::endl;
                        std::cout << "(Для выхода: /exit_chat)" << std::endl << std::endl;
                        handled = true;
                    }
                    else if (prefix == "NO_HISTORY") {
                        G_inChatMode = true; G_inGroupChatMode = false; G_waitingForChatInitiation = false;
                        chat_history_loading = false; chat_target_loading.clear();
                        clearConsoleScreen();
                        std::cout << "--- Чат с " << G_currentChatPartner << " ---" << std::endl;
                        std::cout << "(Для выхода: /exit_chat)" << std::endl << std::endl;
                        std::cout << "[СИСТЕМА] Нет сообщений с '" << payload << "'." << std::endl;
                        handled = true;
                    }
                }
                // --- Обработка инициации группового чата (когда ждем GROUP_HISTORY_START или NO_GROUP_HISTORY) ---
                else if (G_waitingForChatInitiation.load() && !G_inChatMode.load() && !G_currentGroupName.empty() && G_currentGroupName == payload) {
                    if (prefix == "GROUP_HISTORY_START") {
                        G_inGroupChatMode = true; G_inChatMode = false; G_waitingForChatInitiation = false;
                        chat_history_loading = true; chat_target_loading = payload;
                        clearConsoleScreen();
                        std::cout << "--- Групповой чат: " << G_currentGroupName << " ---" << std::endl;
                        std::cout << "(Для выхода: /exit_chat)" << std::endl << std::endl;
                        handled = true;
                    }
                    else if (prefix == "NO_GROUP_HISTORY") {
                        G_inGroupChatMode = true; G_inChatMode = false; G_waitingForChatInitiation = false;
                        chat_history_loading = false; chat_target_loading.clear();
                        clearConsoleScreen();
                        std::cout << "--- Групповой чат: " << G_currentGroupName << " ---" << std::endl;
                        std::cout << "(Для выхода: /exit_chat)" << std::endl << std::endl;
                        std::cout << "[СИСТЕМА] Нет сообщений в группе '" << payload << "'." << std::endl;
                        handled = true;
                    }
                }
                // --- Ошибка при инициации чата (пользователь/группа не найдены) ---
                else if (G_waitingForChatInitiation.load() &&
                    (prefix == "ERROR_CMD" || prefix == "ERROR_GROUP_NOT_FOUND" || prefix == "ERROR_NOT_MEMBER")) {
                    // Более общая проверка на ошибку, если ждем инициации
                    std::string targetName = G_inGroupChatMode.load() ? G_currentGroupName : G_currentChatPartner;
                    if (targetName.empty() && G_waitingForChatInitiation.load()) { // Если цель неясна, но ждем
                        targetName = (payload.find("Group") != std::string::npos || prefix.find("GROUP") != std::string::npos) ?
                            G_currentGroupName : G_currentChatPartner; // Пытаемся угадать
                    }
                    std::cout << "[СИСТЕМА] Не удалось войти в чат/группу '" << targetName << "'. Сервер: " << message << std::endl;
                    G_inChatMode = false; G_inGroupChatMode = false;
                    G_currentChatPartner.clear(); G_currentGroupName.clear();
                    G_waitingForChatInitiation = false;
                    handled = true;
                }
                // --- Список друзей (личные чаты) ---
                else if (prefix == "FRIEND_LIST_START") { G_isReceivingFriendList = true; std::cout << "--- Ваши личные чаты (друзья) ---" << std::endl; handled = true; }
                else if (prefix == "FRIEND" && G_isReceivingFriendList.load()) {
                    std::istringstream iss(payload); std::string name, status; iss >> name >> status;
                    std::cout << "  " << name << " (" << status << ")" << std::endl; handled = true;
                }
                else if (prefix == "FRIEND_LIST_END" && G_isReceivingFriendList.load()) { G_isReceivingFriendList = false; std::cout << "--------------------------------" << std::endl; handled = true; }
                else if (prefix == "NO_FRIENDS_FOUND") { G_isReceivingFriendList = false; std::cout << "[СИСТЕМА] Нет активных личных чатов." << std::endl; handled = true; }

                // --- Список групп ---
                else if (prefix == "MY_GROUPS_START") { G_isReceivingGroupList = true; std::cout << "--- Ваши группы ---" << std::endl; handled = true; }
                else if (prefix == "MY_GROUP_ENTRY" && G_isReceivingGroupList.load()) { std::cout << "  - " << payload << std::endl; handled = true; }
                else if (prefix == "MY_GROUPS_END" && G_isReceivingGroupList.load()) { G_isReceivingGroupList = false; std::cout << "-----------------" << std::endl; handled = true; }
                else if (prefix == "NO_GROUPS_JOINED") { G_isReceivingGroupList = false; std::cout << "[СИСТЕМА] Вы не состоите в группах." << std::endl; handled = true; }

                // --- Сообщения в активном личном чате ---
                else if (G_inChatMode.load() && !G_inGroupChatMode.load()) {
                    if (prefix == "HIST_MSG" && chat_history_loading && chat_target_loading == G_currentChatPartner) {
                        // payload это: timestamp:sender:message_text
                        std::string ts, sender, msg_text;
                        std::istringstream iss_hist(payload); // Используем новый istringstream
                        std::getline(iss_hist, ts, ':');
                        std::getline(iss_hist, sender, ':');
                        std::getline(iss_hist, msg_text);
                        displayChatMessageClient(ts, sender, msg_text);
                        handled = true;
                    }
                    else if (prefix == "HISTORY_END" && payload == G_currentChatPartner && chat_history_loading) {
                        chat_history_loading = false; chat_target_loading.clear();
                        handled = true;
                    }
                    else if (prefix == "MSG_FROM") { // payload это: sender_user: message_text
                        std::string sender_user, message_text;
                        size_t colon_pos = payload.find(':');
                        if (colon_pos != std::string::npos) {
                            sender_user = payload.substr(0, colon_pos);
                            // Убедимся, что есть что-то после ": "
                            if (colon_pos + 2 <= payload.length()) message_text = payload.substr(colon_pos + 2);

                            if (sender_user == G_currentChatPartner) { // Сообщение от текущего собеседника
                                displayChatMessageClient(getCurrentLocalTimestampForChatDisplay(), sender_user, message_text);
                            }
                            else { // Сообщение от другого пользователя, пока мы в этом чате (редко, но возможно)
                                std::cout << "<< " << payload << " >>" << std::endl;
                            }
                        }
                        else { /* Ошибка формата, сервер должен слать "sender: text" */ }
                        handled = true;
                    }
                }
                // --- Сообщения в активном групповом чате ---
                else if (G_inGroupChatMode.load()) {
                    if (prefix == "GROUP_HIST_MSG" && chat_history_loading && chat_target_loading == G_currentGroupName) {
                        // payload это: timestamp:sender:message_text
                        std::string ts, sender, msg_text;
                        std::istringstream iss_hist(payload);
                        std::getline(iss_hist, ts, ':');
                        std::getline(iss_hist, sender, ':');
                        std::getline(iss_hist, msg_text);
                        displayChatMessageClient(ts, sender, msg_text);
                        handled = true;
                    }
                    else if (prefix == "GROUP_HISTORY_END" && payload == G_currentGroupName && chat_history_loading) {
                        chat_history_loading = false; chat_target_loading.clear();
                        handled = true;
                    }
                    else if (prefix == "GROUP_MSG_FROM") {
                        // payload это: groupNamePart sender_user: msg_text_part
                        std::string groupNamePart, senderAndText;
                        std::istringstream iss_group_msg(payload);
                        iss_group_msg >> groupNamePart; // Читаем имя группы
                        iss_group_msg >> std::ws;       // Пропускаем пробел
                        std::getline(iss_group_msg, senderAndText); // Остальное - "sender: text"

                        if (groupNamePart == G_currentGroupName) { // Сообщение для текущей активной группы
                            std::string sender_user, msg_text_part;
                            size_t colon_pos = senderAndText.find(':');
                            if (colon_pos != std::string::npos) {
                                sender_user = senderAndText.substr(0, colon_pos);
                                if (colon_pos + 2 <= senderAndText.length()) msg_text_part = senderAndText.substr(colon_pos + 2);
                                displayChatMessageClient(getCurrentLocalTimestampForChatDisplay(), sender_user, msg_text_part);
                            }
                            else { /* Ошибка формата от сервера */ }
                        }
                        else { // Сообщение для другой группы, не активной сейчас
                            std::cout << "<< Новое в группе '" << groupNamePart << "': " << senderAndText << " >>" << std::endl;
                        }
                        handled = true;
                    }
                    else if (prefix == "USER_JOINED_GROUP" || prefix == "INFO_ADDED_TO_GROUP") { // payload: <GroupName> <Username>
                        std::string group_name, user_name;
                        std::istringstream iss_join(payload);
                        iss_join >> group_name >> user_name;
                        if (group_name == G_currentGroupName) { // Уведомление для текущей группы
                            std::cout << "[ГРУППА] " << user_name << " присоединился." << std::endl;
                        }
                        else { // Уведомление для другой группы
                            std::cout << "[СИСТЕМА] " << user_name << " присоединился к '" << group_name << "'." << std::endl;
                        }
                        handled = true;
                    }
                }

                // --- Общие ответы сервера, не связанные с активным чатом или списками ---
                if (!handled) { // Если сообщение не было обработано выше
                    if (message.rfind("OK_LOGIN", 0) == 0 || message.rfind("OK_REGISTERED", 0) == 0) {
                        G_loggedIn = true;
                        G_currentUsername = parseUsernameFromWelcome(message);
                        if (G_currentUsername.empty() && G_loggedIn.load()) G_currentUsername = "User"; // Fallback
                        clearConsoleScreen(); printWelcomeMessage();
                        std::cout << "Вы успешно вошли как " << G_currentUsername << "!" << std::endl;
                        std::string target = G_inGroupChatMode.load() ? G_currentGroupName : (G_inChatMode.load() ? G_currentChatPartner : "");
                        printHelp(G_loggedIn.load(), G_inChatMode.load(), G_inGroupChatMode.load(), target);
                    }
                    // Ответ на LOGOUT (если пришел до того, как основной поток обработал G_clientRunning = false)
                    else if (!G_currentUsername.empty() && message.rfind("OK_LOGOUT Goodbye, " + G_currentUsername, 0) == 0) {
                        bool wasInAnyChat = G_inChatMode.load() || G_inGroupChatMode.load();
                        G_loggedIn = false; G_currentUsername.clear();
                        G_inChatMode = false; G_currentChatPartner.clear();
                        G_inGroupChatMode = false; G_currentGroupName.clear();
                        G_waitingForChatInitiation = false; // Сброс всех флагов
                        G_isReceivingFriendList = false; G_isReceivingGroupList = false;
                        chat_history_loading = false; chat_target_loading.clear();
                        if (wasInAnyChat) clearConsoleScreen(); // Очистить экран, если были в чате
                        std::cout << "[СИСТЕМА] Вы вышли из учетной записи." << std::endl;
                        printHelp(G_loggedIn.load(), false, false, ""); // Показать справку для неавторизованного
                    }
                    else if (message.rfind("OK_GROUP_CREATED", 0) == 0) { std::cout << "[СИСТЕМА] Группа '" << payload << "' успешно создана." << std::endl; }
                    else if (message.rfind("OK_JOINED_GROUP", 0) == 0) { std::cout << "[СИСТЕМА] Вы присоединились к группе '" << payload << "'." << std::endl; }
                    else if (message.rfind("OK_SENT", 0) == 0) { /* Сообщение о доставке, можно игнорировать в выводе */ }
                    else if (message.rfind("OK_GROUP_MSG_SENT", 0) == 0) { /* Сообщение о доставке в группу, можно игнорировать в выводе */ }
                    else if (message.rfind("ERROR_", 0) == 0) { // Общие ошибки
                        if (G_waitingForChatInitiation.load()) { // Если ошибка пришла во время ожидания открытия чата
                            std::cout << "[ОТВЕТ СЕРВЕРА ПРИ ОТКРЫТИИ ЧАТА] " << message << std::endl;
                            G_waitingForChatInitiation = false; // Сбросить флаг ожидания
                            G_currentChatPartner.clear(); G_currentGroupName.clear(); // Сбросить цели чата
                        }
                        else if (!G_inChatMode.load() && !G_inGroupChatMode.load()) { // Если не в чате и не ждем открытия
                            std::cout << "[ОТВЕТ СЕРВЕРА] " << message << std::endl;
                        }
                        // Если в чате, ошибки могут быть специфичными (например, ERROR_NOT_MEMBER при отправке)
                        // и должны обрабатываться там, либо здесь как общий случай, если не были.
                        // Сейчас они там не обрабатываются, поэтому выводятся тут.
                        else { std::cout << "[ОТВЕТ СЕРВЕРА] " << message << std::endl; }

                    }
                    // Входящее личное сообщение, когда мы не в чате с этим пользователем
                    else if (prefix == "MSG_FROM" && (!G_inChatMode.load() || G_currentChatPartner != payload.substr(0, payload.find(':'))) && !G_inGroupChatMode.load()) {
                        std::cout << "<< " << message << " >>" << std::endl; // Показать как уведомление
                    }
                    // Игнорируем "остатки" истории, если мы уже не в режиме загрузки/ожидания
                    else if ((prefix == "HISTORY_START" || prefix == "HIST_MSG" || prefix == "HISTORY_END" || prefix == "NO_HISTORY" ||
                        prefix == "GROUP_HISTORY_START" || prefix == "GROUP_HIST_MSG" || prefix == "GROUP_HISTORY_END" || prefix == "NO_GROUP_HISTORY")
                        && !G_inChatMode.load() && !G_inGroupChatMode.load() && !G_waitingForChatInitiation.load() && !chat_history_loading) {
                        // Просто игнорируем эти сообщения, если они пришли не вовремя
                    }
                    // Все остальное, что не было опознано
                    else if (prefix != "FRIEND_LIST_START" && prefix != "FRIEND" && prefix != "FRIEND_LIST_END" && prefix != "NO_FRIENDS_FOUND" &&
                        prefix != "MY_GROUPS_START" && prefix != "MY_GROUP_ENTRY" && prefix != "MY_GROUPS_END" && prefix != "NO_GROUPS_JOINED")
                    {
                        // Этот блок ловит все, что не было явно обработано выше
                        // Исключаем состояния явной загрузки списков или ожидания чата
                        if (!G_inChatMode.load() && !G_inGroupChatMode.load() &&
                            !G_waitingForChatInitiation.load() && !G_isReceivingFriendList.load() && !G_isReceivingGroupList.load())
                        {
                            std::cout << "[НЕИЗВЕСТНЫЙ ОТВЕТ СЕРВЕРА] " << message << std::endl;
                        }
                    }
                } // конец if (!handled)
            } // конец else if (!message.empty())

            // Обновляем промпт после обработки сообщения, если клиент все еще работает и не выходит
            if (G_clientRunning.load() && !G_programShouldExit.load()) {
                displayPrompt();
            }
        } // конец if (selectResult > 0 && FD_ISSET)
    } // конец while (G_clientRunning.load())

    // Сообщение о завершении потока, если это не полный выход из программы
    if (!G_programShouldExit.load()) {
        std::lock_guard<std::mutex> lock(G_coutMutex);
        std::cout << "\r" << std::string(120, ' ') << "\r";
        std::cout << "[ПРИЕМНИК] Поток приема сообщений завершен." << std::endl;
    }
}


int main() {
#ifdef _WIN32 // Настройка кодировки консоли для Windows
    SetConsoleCP(1251); SetConsoleOutputCP(1251);
    WSADATA wsaData; if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { std::cerr << "[СИСТЕМА] WSAStartup не удался." << std::endl; return 1; }
#endif

    // Основной цикл программы: позволяет переподключаться после разрыва соединения
    while (!G_programShouldExit.load()) {
        // Сброс флагов состояния перед новой попыткой подключения (если это не первый запуск)
        G_clientRunning = true;
        G_waitingForChatInitiation = false;
        G_isReceivingFriendList = false;
        G_isReceivingGroupList = false;
        // G_loggedIn и G_currentUsername сбрасываются при реальном дисконнекте/logout

        if (G_clientSocket == INVALID_SOCKET_VALUE) { // Если сокет не создан или был закрыт
            G_clientSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (G_clientSocket == INVALID_SOCKET_VALUE) { std::cerr << "[СИСТЕМА] Ошибка создания сокета." << std::endl; return 1; }

            sockaddr_in serverAddress;
            serverAddress.sin_family = AF_INET;
            serverAddress.sin_port = htons(8081); // Порт сервера
            // =================================================================
            // ========== IP АДРЕС СЕРВЕРА - ИЗМЕНИТЕ ПРИ НЕОБХОДИМОСТИ ==========
            const char* server_ip = "192.168.0.24"; // или "192.168.0.24" и т.д.
            // =================================================================
#ifdef _WIN32
            if (inet_pton(AF_INET, server_ip, &serverAddress.sin_addr) <= 0) {
                std::cerr << "[СИСТЕМА] inet_pton ошибка для IP: " << server_ip << std::endl;
                CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; WSACleanup(); return 1;
            }
#else
            if (inet_aton(server_ip, &serverAddress.sin_addr) == 0) {
                std::cerr << "[СИСТЕМА] inet_aton ошибка для IP: " << server_ip << std::endl;
                CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; return 1;
            }
#endif
            {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                std::cout << "[СИСТЕМА] Попытка подключения к серверу " << server_ip << ":" << ntohs(serverAddress.sin_port) << "..." << std::endl;
            }
            if (connect(G_clientSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR_VALUE) {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                clearConsoleScreen();
                std::cerr << "[СИСТЕМА] Подключение к серверу не удалось: " << GET_LAST_ERROR << std::endl;
                std::cerr << "Нажмите Enter для переподключения или введите EXIT для выхода." << std::endl;
                CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; // Важно сбросить сокет

                std::string temp_input;
                std::getline(std::cin, temp_input); // Ожидаем ввода от пользователя
                std::string temp_input_upper = temp_input;
                std::transform(temp_input_upper.begin(), temp_input_upper.end(), temp_input_upper.begin(),
                    [](unsigned char c) { return std::toupper(c); });
                if (temp_input_upper == "EXIT") G_programShouldExit = true; // Если пользователь ввел EXIT
                continue; // Переход к следующей итерации цикла while (!G_programShouldExit.load())
            }
            { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Успешно подключено." << std::endl; }
        }

        std::thread receiverThread;
        if (!G_programShouldExit.load() && G_clientSocket != INVALID_SOCKET_VALUE) {
            receiverThread = std::thread(receiveMessagesThreadFunc); // Запускаем поток приемника
        }
        else if (G_programShouldExit.load()) { // Если уже принято решение о выходе
            break;
        }

        std::string lineInput; // Для ввода команд пользователя
        // Отображение начального экрана/справки при первом подключении или после переподключения (если не залогинен)
        if (!G_programShouldExit.load() && !G_loggedIn.load()) {
            printInitialScreen();
        }
        // Если залогинен, но не в чате - показать общую справку и промпт
        else if (!G_programShouldExit.load() && !G_inChatMode.load() && !G_inGroupChatMode.load()) {
            std::lock_guard<std::mutex> lock(G_coutMutex);
            printHelp(G_loggedIn.load(), false, false, "");
            displayPrompt();
        }
        // Если в чате, промпт уже отображен потоком приемника при входе в чат

        // Цикл обработки команд пользователя
        while (G_clientRunning.load() && !G_programShouldExit.load()) {
            if (!std::getline(std::cin, lineInput)) { // Ошибка ввода или EOF
                if (std::cin.eof()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "\n[СИСТЕМА] EOF получен. Завершение..." << std::endl; }
                else { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "\n[СИСТЕМА] Ошибка ввода. Завершение..." << std::endl; }
                G_programShouldExit = true; // Инициируем полный выход
                G_clientRunning = false;    // Останавливаем этот цикл и поток приемника
                break;
            }
            if (!G_clientRunning.load() || G_programShouldExit.load()) break; // Дополнительная проверка флагов

            // --- Режим личного чата ---
            if (G_inChatMode.load() && !G_inGroupChatMode.load()) {
                if (lineInput == "/exit_chat") {
                    G_inChatMode = false;
                    std::string exitedPartner = G_currentChatPartner; G_currentChatPartner.clear();
                    G_waitingForChatInitiation = false; // Сброс на всякий случай
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    clearConsoleScreen();
                    std::cout << "[СИСТЕМА] Вы покинули чат с " << exitedPartner << "." << std::endl;
                    printHelp(G_loggedIn.load(), false, false, ""); // Показать общую справку
                    displayPrompt();
                }
                else if (!lineInput.empty()) { // Отправка сообщения в личный чат
                    if (G_clientSocket != INVALID_SOCKET_VALUE) {
                        clientSendMessage(G_clientSocket, "SEND_PRIVATE " + G_currentChatPartner + " " + lineInput);
                        std::lock_guard<std::mutex> lock(G_coutMutex);
                        std::cout << "\r" << std::string(120, ' ') << "\r"; // Очистка строки
                        displayChatMessageClient(getCurrentLocalTimestampForChatDisplay(), G_currentUsername, lineInput); // Отображаем свое сообщение
                        displayPrompt();
                    }
                    else {
                        std::lock_guard<std::mutex> lock(G_coutMutex);
                        std::cout << "[СИСТЕМА] Нет соединения для отправки." << std::endl; displayPrompt();
                    }
                }
                else { // Пустой ввод в чате - просто обновить промпт
                    std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt();
                }
                continue; // Пропускаем дальнейший парсинг команд
            }
            // --- Режим группового чата ---
            else if (G_inGroupChatMode.load()) {
                if (lineInput == "/exit_chat") {
                    G_inGroupChatMode = false;
                    std::string exitedGroup = G_currentGroupName; G_currentGroupName.clear();
                    G_waitingForChatInitiation = false;
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    clearConsoleScreen();
                    std::cout << "[СИСТЕМА] Вы покинули группу '" << exitedGroup << "'." << std::endl;
                    printHelp(G_loggedIn.load(), false, false, "");
                    displayPrompt();
                }
                else if (!lineInput.empty()) { // Отправка сообщения в группу
                    if (G_clientSocket != INVALID_SOCKET_VALUE) {
                        clientSendMessage(G_clientSocket, "SEND_GROUP " + G_currentGroupName + " " + lineInput);
                        std::lock_guard<std::mutex> lock(G_coutMutex);
                        std::cout << "\r" << std::string(120, ' ') << "\r";
                        displayChatMessageClient(getCurrentLocalTimestampForChatDisplay(), G_currentUsername, lineInput);
                        displayPrompt();
                    }
                    else {
                        std::lock_guard<std::mutex> lock(G_coutMutex);
                        std::cout << "[СИСТЕМА] Нет соединения для отправки." << std::endl; displayPrompt();
                    }
                }
                else {
                    std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt();
                }
                continue; // Пропускаем дальнейший парсинг команд
            }

            // --- Обработка команд вне режима чата ---
            std::string cmd_token, cmd_args;
            size_t first_space = lineInput.find(' ');
            if (first_space != std::string::npos) { // Команда с аргументами
                cmd_token = lineInput.substr(0, first_space);
                if (first_space + 1 < lineInput.length()) cmd_args = lineInput.substr(first_space + 1);
            }
            else { // Команда без аргументов
                cmd_token = lineInput;
            }
            std::string cmd_token_upper = cmd_token; // Для регистронезависимого сравнения
            std::transform(cmd_token_upper.begin(), cmd_token_upper.end(), cmd_token_upper.begin(),
                [](unsigned char c) { return ::toupper(c); });

            if (lineInput.empty()) { std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); continue; }

            bool logout_initiated_by_user = false; // Флаг, что пользователь ввел EXIT/LOGOUT

            if (cmd_token_upper == "EXIT") {
                if (G_loggedIn.load()) { // Если залогинен, сначала отправляем LOGOUT серверу
                    if (G_clientSocket != INVALID_SOCKET_VALUE) clientSendMessage(G_clientSocket, "LOGOUT");
                    logout_initiated_by_user = true; // Флаг для ожидания ответа от сервера и корректного выхода
                    // G_clientRunning = false будет установлено после ожидания или таймаута
                }
                else { // Если не залогинен, просто выходим из программы
                    G_programShouldExit = true;
                    G_clientRunning = false;
                }
            }
            else if (cmd_token_upper == "HELP") {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                std::string target = G_inGroupChatMode.load() ? G_currentGroupName : (G_inChatMode.load() ? G_currentChatPartner : "");
                printHelp(G_loggedIn.load(), G_inChatMode.load(), G_inGroupChatMode.load(), target);
                displayPrompt();
            }
            else if (cmd_token_upper == "FRIENDS") { // Запрос списка друзей
                if (!G_loggedIn.load()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Сначала войдите в систему." << std::endl; displayPrompt(); }
                else if (G_clientSocket != INVALID_SOCKET_VALUE) { clientSendMessage(G_clientSocket, "GET_CHAT_PARTNERS"); std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); }
                else { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Нет соединения." << std::endl; displayPrompt(); }
            }
            else if (cmd_token_upper == "CREATE_GROUP") {
                if (!G_loggedIn.load()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Сначала войдите." << std::endl; displayPrompt(); }
                else if (cmd_args.empty()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Укажите название группы: CREATE_GROUP <название>" << std::endl; displayPrompt(); }
                else { clientSendMessage(G_clientSocket, "CREATE_GROUP " + cmd_args); std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); }
            }
            else if (cmd_token_upper == "JOIN_GROUP") {
                if (!G_loggedIn.load()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Сначала войдите." << std::endl; displayPrompt(); }
                else if (cmd_args.empty()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Укажите название группы: JOIN_GROUP <название>" << std::endl; displayPrompt(); }
                else { clientSendMessage(G_clientSocket, "JOIN_GROUP " + cmd_args); std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); }
            }
            else if (cmd_token_upper == "GROUPCHAT") { // Вход в групповой чат (запрос истории)
                if (!G_loggedIn.load()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Сначала войдите." << std::endl; displayPrompt(); }
                else if (cmd_args.empty()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Укажите название группы: GROUPCHAT <название>" << std::endl; displayPrompt(); }
                else {
                    std::lock_guard<std::mutex> lock(G_coutMutex); // Защищаем G_currentGroupName и др.
                    G_currentGroupName = cmd_args;
                    G_waitingForChatInitiation = true; // Ожидаем ответа с историей
                    G_inChatMode = false; G_currentChatPartner.clear(); // Выходим из личного чата, если были

                    clientSendMessage(G_clientSocket, "GROUPCHAT " + G_currentGroupName);
                    std::cout << "\r" << std::string(120, ' ') << "\r";
                    std::cout << "[СИСТЕМА] Запрос группового чата '" << G_currentGroupName << "'..." << std::endl;
                    displayPrompt();
                }
            }
            else if (cmd_token_upper == "LIST_MY_GROUPS") {
                if (!G_loggedIn.load()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Сначала войдите." << std::endl; displayPrompt(); }
                else { clientSendMessage(G_clientSocket, "LIST_MY_GROUPS"); std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); }
            }
            else if (cmd_token_upper == "CHAT") { // Вход в личный чат (запрос истории)
                if (!G_loggedIn.load()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Сначала войдите." << std::endl; displayPrompt(); }
                else if (cmd_args.empty()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Укажите имя пользователя: CHAT <username>" << std::endl; displayPrompt(); }
                else if (cmd_args == G_currentUsername) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Нельзя начать чат с самим собой." << std::endl; displayPrompt(); }
                else {
                    if (G_clientSocket != INVALID_SOCKET_VALUE) {
                        std::lock_guard<std::mutex> lock(G_coutMutex);
                        G_currentChatPartner = cmd_args;
                        G_waitingForChatInitiation = true;
                        G_inGroupChatMode = false; G_currentGroupName.clear(); // Выходим из группового, если были

                        clientSendMessage(G_clientSocket, "GET_HISTORY " + G_currentChatPartner);
                        std::cout << "\r" << std::string(120, ' ') << "\r";
                        std::cout << "[СИСТЕМА] Запрос чата с " << G_currentChatPartner << "..." << std::endl;
                        displayPrompt();
                    }
                    else { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Нет соединения." << std::endl; displayPrompt(); }
                }
            }
            else if (cmd_token_upper == "LOGIN" || cmd_token_upper == "REGISTRATION") {
                std::string msg_to_send = cmd_token_upper; // LOGIN или REGISTRATION
                if (!cmd_args.empty()) msg_to_send += " " + cmd_args; // Добавляем <username> <password>

                if (G_clientSocket == INVALID_SOCKET_VALUE) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Нет соединения." << std::endl; displayPrompt(); }
                else { clientSendMessage(G_clientSocket, msg_to_send); std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); }
            }
            else { // Неизвестная команда
                std::lock_guard<std::mutex> lock(G_coutMutex);
                std::cout << "[СИСТЕМА] Неизвестная команда: '" << lineInput << "'" << std::endl;
                displayPrompt();
            }

            // Обработка выхода по команде EXIT/LOGOUT
            if (logout_initiated_by_user) {
                // Даем потоку приемника шанс обработать OK_LOGOUT от сервера
                auto logout_start_time = std::chrono::steady_clock::now();
                bool server_confirmed_logout = false;
                while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - logout_start_time).count() < 700) { // Таймаут ожидания
                    if (!G_loggedIn.load()) { // Если G_loggedIn стал false (обработано потоком приемника)
                        server_confirmed_logout = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                if (!server_confirmed_logout) { // Если сервер не подтвердил выход или таймаут
                    std::lock_guard<std::mutex> lock(G_coutMutex); // Защищаем сброс глобальных переменных
                    // Принудительно сбрасываем состояние, так как сервер мог не ответить или ответ потерялся
                    G_loggedIn = false; G_currentUsername.clear();
                    G_inChatMode = false; G_currentChatPartner.clear();
                    G_inGroupChatMode = false; G_currentGroupName.clear();
                    G_waitingForChatInitiation = false;
                    std::cout << "[СИСТЕМА] Выход из учетной записи (таймаут ответа от сервера)." << std::endl;
                }
                G_clientRunning = false; // Останавливаем основной цикл и поток приемника для этой сессии
                // Это приведет к переподключению или выходу из программы, если G_programShouldExit true
            }
        } // конец while (G_clientRunning.load() && !G_programShouldExit.load()) - цикл ввода команд

        // Завершение текущей сессии клиента (не обязательно всей программы)
        G_clientRunning = false; // Сигнал потоку приемника на завершение
        if (receiverThread.joinable()) {
            receiverThread.join(); // Ожидаем завершения потока приемника
        }

        // Закрываем сокет, если он был открыт и программа не должна полностью завершаться
        // (чтобы при следующей итерации внешнего цикла создался новый)
        // Если G_programShouldExit true, сокет закроется перед выходом из main.
        if (G_clientSocket != INVALID_SOCKET_VALUE && !G_programShouldExit.load()) {
            CLOSE_SOCKET(G_clientSocket);
            G_clientSocket = INVALID_SOCKET_VALUE;
        }
        else if (G_programShouldExit.load() && G_clientSocket != INVALID_SOCKET_VALUE) {
            // Если полный выход, тоже закрываем сокет здесь, чтобы не делать это дважды.
            CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE;
        }

        // Сброс состояний перед возможным переподключением, если программа не завершается
        if (!G_programShouldExit.load()) {
            // Эти флаги уже должны быть false после выхода из чатов/логаута, но для надежности
            G_loggedIn = false; G_currentUsername.clear();
            G_inChatMode = false; G_currentChatPartner.clear();
            G_inGroupChatMode = false; G_currentGroupName.clear();
            G_waitingForChatInitiation = false;
            G_isReceivingFriendList = false; G_isReceivingGroupList = false;
        }
    } // конец while (!G_programShouldExit.load()) - главный цикл программы

    // Финальное завершение
    {
        std::lock_guard<std::mutex> lock(G_coutMutex);
        std::cout << "\r" << std::string(120, ' ') << "\r";
        std::cout << "[СИСТЕМА] Завершение работы клиента..." << std::endl;
    }
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[СИСТЕМА] Клиент завершил работу. До новых встреч!" << std::endl;
    return 0;
}