#include <iostream>
#include <string>
#include <thread>
#include <sstream>
#include <chrono>
#include <atomic>    // для std::atomic_bool
#include <mutex>     // для std::mutex
#include <algorithm> // для std::remove, std::transform
#include <vector>    // для хранения команд
#include <cctype>    // для std::toupper
#include <iomanip>   // для std::put_time
#include <map>       // Для информации о непрочитанных чатах

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

// Глобальные переменные
SocketType G_clientSocket = INVALID_SOCKET_VALUE;
std::atomic<bool> G_clientRunning(true);
std::atomic<bool> G_programShouldExit(false);
std::mutex G_coutMutex;
std::atomic<bool> G_loggedIn(false);
std::string G_currentUsername;
std::atomic<bool> G_inChatMode(false);
std::string G_currentChatPartner;
std::atomic<bool> G_waitingForChatInitiation(false);
std::atomic<bool> G_isReceivingFriendList(false);


// --- ПРОТОТИПЫ ФУНКЦИЙ ИНТЕРФЕЙСА ---
void clearConsoleScreen();
void printWelcomeMessage();
void printHelp(bool isLoggedIn, bool isInChatMode, const std::string& chatPartner);
void displayPrompt();
void printInitialScreen();
void displayChatMessageClient(const std::string& timestamp_str, const std::string& sender, const std::string& message_text);
std::string formatTimestampForDisplay(const std::string& full_timestamp_from_server);
std::string getCurrentLocalTimestampForChatDisplay();
// --- КОНЕЦ ПРОТОТИПОВ ---


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

void printHelp(bool isLoggedIn, bool isInChatMode, const std::string& chatPartner) {
    std::cout << "\n--- Доступные команды ---\n";
    if (isInChatMode) {
        std::cout << "  Вы находитесь в чате с " << chatPartner << ".\n";
        std::cout << "  Просто вводите текст и нажимайте Enter для отправки сообщения.\n";
        std::cout << "  /exit_chat - Покинуть текущий чат и вернуться к основному меню.\n";
    }
    else if (!isLoggedIn) {
        std::cout << "  LOGIN <имя_пользователя> <пароль> - Войти в систему\n";
        std::cout << "  REGISTRATION <имя_пользователя> <пароль> - Зарегистрировать нового пользователя\n";
        std::cout << "  HELP - Показать это сообщение помощи\n";
        std::cout << "  EXIT - Выйти из программы\n";
    }
    else {
        std::cout << "  CHAT <имя_пользователя> - Открыть чат с пользователем.\n";
        std::cout << "  FRIENDS - Показать список ваших чатов (друзей) и их статус.\n";
        std::cout << "  HELP - Показать это сообщение помощи\n";
        std::cout << "  EXIT - Выйти из текущей учетной записи\n";
    }
    std::cout << "-------------------------\n" << std::endl;
}


void displayPrompt() {
    std::cout << "\r" << std::string(120, ' ') << "\r";
    if (G_inChatMode.load()) {
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
    printHelp(G_loggedIn.load(), G_inChatMode.load(), G_currentChatPartner);
    displayPrompt();
}

std::string formatTimestampForDisplay(const std::string& full_timestamp_from_server) {
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
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm buf_today;
#ifdef _WIN32
        localtime_s(&buf_today, &in_time_t);
#else
        localtime_r(&in_time_t, &buf_today);
#endif
        std::stringstream today_ss;
        today_ss << std::setfill('0') << std::setw(2) << buf_today.tm_mday << "."
            << std::setfill('0') << std::setw(2) << (buf_today.tm_mon + 1);
        std::string today_date_str = today_ss.str();
        if (formatted_date == today_date_str) {
            return formatted_time;
        }
        else {
            return "[" + formatted_date + " | " + formatted_time + "]";
        }
    }
    if (full_timestamp_from_server.length() == 5 && full_timestamp_from_server[2] == ':') { // Уже ЧЧ:ММ
        return full_timestamp_from_server;
    }
    return full_timestamp_from_server; // Fallback
}

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

void displayChatMessageClient(const std::string& timestamp_str, const std::string& sender, const std::string& message_text) {
    std::string display_ts = formatTimestampForDisplay(timestamp_str);
    std::cout << display_ts << " ";
    if (sender == G_currentUsername) {
        std::cout << "Вы: ";
    }
    else {
        std::cout << sender << ": ";
    }
    std::cout << message_text << std::endl;
}


std::string clientReadLine(SocketType socket) {
    std::string line;
    char ch;
    while (G_clientRunning.load()) {
        int bytesReceived = recv(socket, &ch, 1, 0);
        if (bytesReceived == 0) {
            return "";
        }
        if (bytesReceived < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAESHUTDOWN || WSAGetLastError() == WSAENOTSOCK || WSAGetLastError() == WSAEINTR) {
                return "";
            }
#else
            if (errno == ECONNRESET || errno == EPIPE || errno == EBADF || errno == EINTR) {
                return "";
            }
#endif
            return "";
        }
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            line += ch;
        }
    }
    return line;
}

void clientSendMessage(SocketType socket, const std::string& message) {
    if (socket == INVALID_SOCKET_VALUE || !G_clientRunning.load()) return;
    std::string cleanedMessage = message;
    cleanedMessage.erase(std::remove(cleanedMessage.begin(), cleanedMessage.end(), '\r'), cleanedMessage.end());
    std::string msg_to_send = cleanedMessage + "\n";
    if (send(socket, msg_to_send.c_str(), static_cast<int>(msg_to_send.length()), 0) == SOCKET_ERROR_VALUE) {
        std::lock_guard<std::mutex> lock(G_coutMutex);
        std::cout << "\r" << std::string(120, ' ') << "\r";
        std::cerr << "[СИСТЕМА] Ошибка отправки: " << GET_LAST_ERROR << ". Соединение может быть разорвано." << std::endl;
        displayPrompt();
    }
}

std::string parseUsernameFromWelcome(const std::string& serverResponse) {
    std::string prefix1 = "OK_LOGIN Welcome, ";
    std::string prefix2 = "OK_REGISTERED Welcome, ";
    std::string suffix = "!";
    std::string username = "";
    size_t startPos = std::string::npos;
    if (serverResponse.rfind(prefix1, 0) == 0) startPos = prefix1.length();
    else if (serverResponse.rfind(prefix2, 0) == 0) startPos = prefix2.length();
    if (startPos != std::string::npos) {
        size_t endPos = serverResponse.rfind(suffix);
        if (endPos != std::string::npos && endPos > startPos) {
            username = serverResponse.substr(startPos, endPos - startPos);
        }
    }
    return username;
}


void receiveMessagesThreadFunc() {
    fd_set readSet;
    timeval timeout;
    bool chat_history_loading = false;
    std::string chat_history_partner_loading;

    while (G_clientRunning.load()) {
        if (G_programShouldExit.load()) break;
        if (G_clientSocket == INVALID_SOCKET_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        FD_ZERO(&readSet); FD_SET(G_clientSocket, &readSet);
        timeout.tv_sec = 1; timeout.tv_usec = 0;
        int selectNfds = G_clientSocket + 1;
#ifdef _WIN32
        selectNfds = 0;
#endif
        int selectResult = select(selectNfds, &readSet, nullptr, nullptr, &timeout);

        if (G_programShouldExit.load()) break;
        if (!G_clientRunning.load() && !G_programShouldExit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue;
        }

        if (selectResult < 0) {
            int error_code = GET_LAST_ERROR;
            if (G_clientRunning.load()) {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                clearConsoleScreen();
                std::cerr << "\n[ПРИЕМНИК] select ошибка " << error_code << " или сокет закрыт. Попробуйте перезапустить клиент." << std::endl;
                std::cout << "Нажмите Enter для выхода из программы..." << std::flush;
            }
            G_loggedIn = false; G_currentUsername.clear();
            G_inChatMode = false; G_currentChatPartner.clear();
            G_waitingForChatInitiation = false; G_isReceivingFriendList = false;
            if (G_clientSocket != INVALID_SOCKET_VALUE) { CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; }
            G_programShouldExit = true;
            G_clientRunning = false;
            break;
        }

        if (selectResult > 0 && FD_ISSET(G_clientSocket, &readSet)) {
            std::string message = clientReadLine(G_clientSocket);
            std::lock_guard<std::mutex> lock(G_coutMutex);
            if (G_programShouldExit.load() && message.empty()) break;

            if (message.empty() && G_clientRunning.load()) {
                std::cout << "\r" << std::string(120, ' ') << "\r";
                std::cout << "[ПРИЕМНИК] Сервер отключился или ошибка чтения." << std::endl;
                G_loggedIn = false; G_currentUsername.clear();
                G_inChatMode = false; G_currentChatPartner.clear();
                G_waitingForChatInitiation = false; G_isReceivingFriendList = false;
                if (G_clientSocket != INVALID_SOCKET_VALUE) { CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; }
                if (!G_inChatMode.load()) displayPrompt();
            }
            else if (!message.empty()) {
                std::cout << "\r" << std::string(120, ' ') << "\r";
                std::string prefix, payload;
                size_t space_pos = message.find(' ');
                if (space_pos != std::string::npos) {
                    prefix = message.substr(0, space_pos); payload = message.substr(space_pos + 1);
                }
                else { prefix = message; }

                bool message_was_handled = false;

                if (G_waitingForChatInitiation.load() && G_currentChatPartner == payload) {
                    if (prefix == "HISTORY_START") {
                        G_inChatMode = true; G_waitingForChatInitiation = false;
                        chat_history_loading = true; chat_history_partner_loading = payload;
                        clearConsoleScreen();
                        std::cout << "--- Чат с " << G_currentChatPartner << " ---" << std::endl;
                        std::cout << "(Для выхода из чата введите /exit_chat)" << std::endl << std::endl;
                        message_was_handled = true;
                    }
                    else if (prefix == "NO_HISTORY") {
                        G_inChatMode = true; G_waitingForChatInitiation = false;
                        chat_history_loading = false; chat_history_partner_loading.clear();
                        clearConsoleScreen();
                        std::cout << "--- Чат с " << G_currentChatPartner << " ---" << std::endl;
                        std::cout << "(Для выхода из чата введите /exit_chat)" << std::endl << std::endl;
                        std::cout << "[СИСТЕМА] Сообщений с пользователем '" << payload << "' пока нет." << std::endl;
                        message_was_handled = true;
                    }
                }
                else if (G_waitingForChatInitiation.load() &&
                    prefix == "ERROR_CMD" &&
                    payload.find("User '" + G_currentChatPartner + "' does not exist") != std::string::npos)
                {
                    std::cout << "[СИСТЕМА] Пользователь '" << G_currentChatPartner << "' не найден." << std::endl;
                    G_inChatMode = false; G_currentChatPartner.clear();
                    G_waitingForChatInitiation = false;
                    message_was_handled = true;
                }
                else if (prefix == "FRIEND_LIST_START") {
                    G_isReceivingFriendList = true;
                    std::cout << "--- Ваши чаты (друзья) ---" << std::endl;
                    message_was_handled = true;
                }
                else if (prefix == "FRIEND" && G_isReceivingFriendList.load()) {
                    std::istringstream friend_iss(payload);
                    std::string friend_name, status;
                    if (friend_iss >> friend_name >> status) {
                        std::cout << "  " << friend_name << " (";
                        if (status == "online") std::cout << "\033[32mв сети\033[0m";
                        else std::cout << "\033[31mне в сети\033[0m";
                        std::cout << ")" << std::endl;
                    }
                    message_was_handled = true;
                }
                else if (prefix == "FRIEND_LIST_END" && G_isReceivingFriendList.load()) {
                    G_isReceivingFriendList = false;
                    std::cout << "--------------------------" << std::endl;
                    message_was_handled = true;
                }
                else if (prefix == "NO_FRIENDS_FOUND") {
                    G_isReceivingFriendList = false;
                    std::cout << "[СИСТЕМА] У вас пока нет начатых чатов." << std::endl;
                    message_was_handled = true;
                }
                else if (G_inChatMode.load()) {
                    if (prefix == "HIST_MSG" && chat_history_loading && chat_history_partner_loading == G_currentChatPartner) {
                        std::string ts, sender, msg_text;
                        std::istringstream hist_iss(payload);
                        std::getline(hist_iss, ts, ':'); std::getline(hist_iss, sender, ':'); std::getline(hist_iss, msg_text);
                        displayChatMessageClient(ts, sender, msg_text);
                        message_was_handled = true;
                    }
                    else if (prefix == "HISTORY_END" && payload == G_currentChatPartner && chat_history_loading) {
                        chat_history_loading = false; chat_history_partner_loading.clear();
                        message_was_handled = true;
                    }
                    else if (prefix == "MSG_FROM") {
                        std::string sender_user, message_text;
                        size_t colon_pos = payload.find(':');
                        if (colon_pos != std::string::npos) {
                            sender_user = payload.substr(0, colon_pos);
                            if (colon_pos + 2 <= payload.length()) message_text = payload.substr(colon_pos + 2); else message_text = "";
                            if (sender_user == G_currentChatPartner) {
                                displayChatMessageClient(getCurrentLocalTimestampForChatDisplay(), sender_user, message_text);
                            }
                            else {
                                std::cout << "<< Новое сообщение от " << sender_user << ": " << message_text << " >>" << std::endl;
                                std::cout << "   (Вы сейчас в чате с " << G_currentChatPartner
                                    << ". Для ответа введите /exit_chat, затем CHAT " << sender_user << ")" << std::endl;
                            }
                        }
                        else { std::cout << "[ОТВЕТ СЕРВЕРА НЕИЗВЕСТНЫЙ ФОРМАТ MSG_FROM] " << message << std::endl; }
                        message_was_handled = true;
                    }
                }

                if (!message_was_handled) {
                    if (message.rfind("OK_LOGIN", 0) == 0 || message.rfind("OK_REGISTERED", 0) == 0) {
                        G_loggedIn = true; G_currentUsername = parseUsernameFromWelcome(message);
                        if (G_currentUsername.empty() && G_loggedIn) G_currentUsername = "User";
                        clearConsoleScreen(); printWelcomeMessage();
                        std::cout << "Вы успешно вошли как " << G_currentUsername << "!" << std::endl;
                        printHelp(G_loggedIn.load(), false, "");
                    }
                    else if (!G_currentUsername.empty() && message.rfind("OK_LOGOUT Goodbye, " + G_currentUsername, 0) == 0) {
                        bool wasInChat = G_inChatMode.load();
                        G_loggedIn = false; G_currentUsername.clear(); G_inChatMode = false; G_currentChatPartner.clear();
                        G_waitingForChatInitiation = false;
                        if (wasInChat) clearConsoleScreen();
                        std::cout << "[СИСТЕМА] Вы вышли из учетной записи." << std::endl;
                        printHelp(G_loggedIn.load(), false, "");
                    }
                    else if (message.rfind("OK_SENT", 0) == 0) { /* Игнорируем OK_SENT */ }
                    else if (message.rfind("ERROR_", 0) == 0) {
                        if (G_waitingForChatInitiation.load()) {
                            std::cout << "[ОТВЕТ СЕРВЕРА ПРИ ПОПЫТКЕ ОТКРЫТЬ ЧАТ] " << message << std::endl;
                            G_waitingForChatInitiation = false;
                            G_currentChatPartner.clear();
                        }
                        else if (!G_inChatMode.load()) {
                            std::cout << "[ОТВЕТ СЕРВЕРА] " << message << std::endl;
                        }
                    }
                    else if (prefix == "MSG_FROM" && !G_inChatMode.load()) {
                        std::cout << "<< " << message << " >>" << std::endl;
                    }
                    else if ((prefix == "HISTORY_START" || prefix == "HIST_MSG" || prefix == "HISTORY_END" || prefix == "NO_HISTORY")
                        && !G_inChatMode.load() && !G_waitingForChatInitiation.load()) {
                        // Игнорируем
                    }
                    else if (prefix != "FRIEND_LIST_START" && prefix != "FRIEND" && prefix != "FRIEND_LIST_END" && prefix != "NO_FRIENDS_FOUND") { // Доп. проверка, чтобы не дублировать
                        if (!G_inChatMode.load() && !G_waitingForChatInitiation.load() && !G_isReceivingFriendList.load()) {
                            std::cout << "[НЕИЗВЕСТНЫЙ ОТВЕТ СЕРВЕРА] " << message << std::endl;
                        }
                    }
                }
            }
            if (G_clientRunning.load() && !G_programShouldExit.load()) { displayPrompt(); }
        }
    }
    if (!G_programShouldExit.load()) {
        std::lock_guard<std::mutex> lock(G_coutMutex);
        std::cout << "\r" << std::string(120, ' ') << "\r";
        std::cout << "[ПРИЕМНИК] Поток приема сообщений завершен." << std::endl;
    }
}


int main() {
#ifdef _WIN32
    SetConsoleCP(1251); SetConsoleOutputCP(1251);
    WSADATA wsaData; if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { std::cerr << "[СИСТЕМА] WSAStartup не удался." << std::endl; return 1; }
#endif

    while (!G_programShouldExit.load()) {
        G_clientRunning = true; G_waitingForChatInitiation = false; G_isReceivingFriendList = false;
        if (G_clientSocket == INVALID_SOCKET_VALUE) {
            G_clientSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (G_clientSocket == INVALID_SOCKET_VALUE) { std::cerr << "[СИСТЕМА] Ошибка создания сокета." << std::endl; return 1; }
            sockaddr_in serverAddress; serverAddress.sin_family = AF_INET; serverAddress.sin_port = htons(8081);
            // =================================================================
            // ========== IP АДРЕС СЕРВЕРА - ИЗМЕНИТЕ ПРИ НЕОБХОДИМОСТИ ==========
            const char* server_ip = "127.0.0.1";
            // =================================================================
#ifdef _WIN32
            if (inet_pton(AF_INET, server_ip, &serverAddress.sin_addr) <= 0) { std::cerr << "[СИСТЕМА] inet_pton ошибка." << std::endl; CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; WSACleanup(); return 1; }
#else
            if (inet_aton(server_ip, &serverAddress.sin_addr) == 0) { std::cerr << "[СИСТЕМА] inet_aton ошибка." << std::endl; CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; return 1; }
#endif
            { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Попытка подключения к серверу " << server_ip << ":" << ntohs(serverAddress.sin_port) << "..." << std::endl; }
            if (connect(G_clientSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR_VALUE) {
                std::lock_guard<std::mutex> lock(G_coutMutex); clearConsoleScreen();
                std::cerr << "[СИСТЕМА] Подключение к серверу не удалось: " << GET_LAST_ERROR << std::endl;
                std::cerr << "Нажмите Enter для переподключения или введите EXIT." << std::endl;
                CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE;
                std::string temp_input; std::getline(std::cin, temp_input);
                std::string temp_input_upper = temp_input;
                std::transform(temp_input_upper.begin(), temp_input_upper.end(), temp_input_upper.begin(), ::toupper);
                if (temp_input_upper == "EXIT") G_programShouldExit = true;
                continue;
            }
            { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Успешно подключено." << std::endl; }
        }

        std::thread receiverThread;
        if (!G_programShouldExit.load() && G_clientSocket != INVALID_SOCKET_VALUE) {
            receiverThread = std::thread(receiveMessagesThreadFunc);
        }
        else if (G_programShouldExit.load()) { break; }

        std::string lineInput;
        if (!G_programShouldExit.load() && !G_loggedIn.load()) { printInitialScreen(); }
        else if (!G_programShouldExit.load() && !G_inChatMode.load()) { std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); }

        while (G_clientRunning.load() && !G_programShouldExit.load()) {
            if (!std::getline(std::cin, lineInput)) {
                if (std::cin.eof()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "\n[СИСТЕМА] EOF." << std::endl; }
                else { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "\n[СИСТЕМА] Ошибка ввода." << std::endl; }
                G_programShouldExit = true; G_clientRunning = false; break;
            }
            if (!G_clientRunning.load() || G_programShouldExit.load()) break;

            if (G_inChatMode.load()) {
                if (lineInput == "/exit_chat") {
                    G_inChatMode = false; G_waitingForChatInitiation = false;
                    std::string exitedPartner = G_currentChatPartner; G_currentChatPartner.clear();
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    clearConsoleScreen();
                    std::cout << "[СИСТЕМА] Вы покинули чат с " << exitedPartner << "." << std::endl;
                    printHelp(G_loggedIn.load(), false, ""); displayPrompt();
                }
                else if (!lineInput.empty()) {
                    if (G_clientSocket != INVALID_SOCKET_VALUE) {
                        clientSendMessage(G_clientSocket, "SEND_PRIVATE " + G_currentChatPartner + " " + lineInput);
                        std::lock_guard<std::mutex> lock(G_coutMutex);
                        std::cout << "\r" << std::string(120, ' ') << "\r";
                        displayChatMessageClient(getCurrentLocalTimestampForChatDisplay(), G_currentUsername, lineInput);
                        displayPrompt();
                    }
                    else { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Нет соединения." << std::endl; displayPrompt(); }
                }
                else { std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); }
                continue;
            }

            std::string cmd_token, cmd_args;
            size_t first_space = lineInput.find(' ');
            if (first_space != std::string::npos) {
                cmd_token = lineInput.substr(0, first_space);
                if (first_space + 1 < lineInput.length()) cmd_args = lineInput.substr(first_space + 1);
            }
            else { cmd_token = lineInput; }
            std::string cmd_token_upper = cmd_token;
            std::transform(cmd_token_upper.begin(), cmd_token_upper.end(), cmd_token_upper.begin(),
                [](unsigned char c) { return ::toupper(c); });

            if (lineInput.empty()) { std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); continue; }
            bool logout_flow = false;
            if (cmd_token_upper == "EXIT") {
                if (G_loggedIn.load()) {
                    if (G_clientSocket != INVALID_SOCKET_VALUE) clientSendMessage(G_clientSocket, "LOGOUT");
                    logout_flow = true;
                }
                else { G_programShouldExit = true; G_clientRunning = false; }
            }
            else if (cmd_token_upper == "HELP") {
                std::lock_guard<std::mutex> lock(G_coutMutex); printHelp(G_loggedIn.load(), G_inChatMode.load(), G_currentChatPartner); displayPrompt();
            }
            else if (cmd_token_upper == "FRIENDS") {
                if (!G_loggedIn.load()) {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    std::cout << "[СИСТЕМА] Сначала войдите в систему." << std::endl;
                    displayPrompt();
                }
                else if (G_clientSocket != INVALID_SOCKET_VALUE) {
                    clientSendMessage(G_clientSocket, "GET_CHAT_PARTNERS");
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    displayPrompt();
                }
                else {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    std::cout << "[СИСТЕМА] Нет соединения с сервером." << std::endl;
                    displayPrompt();
                }
            }
            else if (cmd_token_upper == "CHAT") {
                if (!G_loggedIn.load()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Сначала войдите." << std::endl; displayPrompt(); }
                else if (cmd_args.empty()) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] CHAT <username>" << std::endl; displayPrompt(); }
                else if (cmd_args == G_currentUsername) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Нельзя чат с собой." << std::endl; displayPrompt(); }
                else {
                    if (G_clientSocket != INVALID_SOCKET_VALUE) {
                        std::lock_guard<std::mutex> lock(G_coutMutex);
                        G_currentChatPartner = cmd_args; G_waitingForChatInitiation = true;
                        clientSendMessage(G_clientSocket, "GET_HISTORY " + G_currentChatPartner);
                        std::cout << "\r" << std::string(120, ' ') << "\r";
                        std::cout << "[СИСТЕМА] Запрос чата с " << G_currentChatPartner << "..." << std::endl;
                        displayPrompt();
                    }
                    else { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Нет соединения." << std::endl; displayPrompt(); }
                }
            }
            else if (cmd_token_upper == "LOGIN" || cmd_token_upper == "REGISTRATION") {
                std::string msg_to_send = cmd_token_upper;
                if (!cmd_args.empty()) msg_to_send += " " + cmd_args;
                if (G_clientSocket == INVALID_SOCKET_VALUE) { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Нет соединения." << std::endl; displayPrompt(); }
                else { clientSendMessage(G_clientSocket, msg_to_send); std::lock_guard<std::mutex> lock(G_coutMutex); displayPrompt(); }
            }
            else {
                std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "[СИСТЕМА] Неизвестная команда: '" << lineInput << "'" << std::endl; displayPrompt();
            }
            if (logout_flow) {
                auto logout_start = std::chrono::steady_clock::now(); bool receiver_logout = false;
                while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - logout_start).count() < 700) {
                    if (!G_loggedIn.load()) { receiver_logout = true; break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (!receiver_logout) {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    G_loggedIn = false; G_currentUsername.clear();
                    G_inChatMode = false; G_currentChatPartner.clear(); // Сброс чата при принудительном логауте
                    G_waitingForChatInitiation = false;
                }
                G_clientRunning = false;
            }
        }
        G_clientRunning = false;
        if (receiverThread.joinable()) { receiverThread.join(); }
        if (G_clientSocket != INVALID_SOCKET_VALUE && !G_programShouldExit.load()) { CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; }
        else if (G_programShouldExit.load() && G_clientSocket != INVALID_SOCKET_VALUE) { CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE; }
        if (!G_programShouldExit.load()) {
            G_loggedIn = false; G_currentUsername.clear();
            G_inChatMode = false; G_currentChatPartner.clear();
            G_waitingForChatInitiation = false; G_isReceivingFriendList = false;
        }
    }
    { std::lock_guard<std::mutex> lock(G_coutMutex); std::cout << "\r" << std::string(120, ' ') << "\r"; std::cout << "[СИСТЕМА] Завершение работы клиента..." << std::endl; }
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[СИСТЕМА] Клиент завершил работу. До новых встреч!" << std::endl;
    return 0;
}