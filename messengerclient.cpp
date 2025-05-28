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

#ifdef _WIN32
// Сначала определяем WIN32_LEAN_AND_MEAN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Затем winsock2.h
#include <winsock2.h>
#include <ws2tcpip.h>
// И только потом windows.h, если он все еще нужен
#include <windows.h> // Для SetConsoleOutputCP, SetConsoleCP, и функций очистки консоли
#pragma comment(lib, "ws2_32.lib")
#else
// Для Linux/macOS порядок не так критичен в этом контексте
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>  // Для errno
#include <string.h> // Для strerror
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

// --- ПРОТОТИПЫ ФУНКЦИЙ ИНТЕРФЕЙСА ---
void clearConsoleScreen();
void printWelcomeMessage();
void printHelp(bool isLoggedIn);
void displayPrompt();
void printInitialScreen();
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

void printHelp(bool isLoggedIn) {
    std::cout << "\n--- Доступные команды ---\n";
    if (!isLoggedIn) {
        std::cout << "  LOGIN <имя_пользователя> <пароль> - Войти в систему\n";
        std::cout << "  REGISTRATION <имя_пользователя> <пароль> - Зарегистрировать нового пользователя\n";
    }
    else {
        std::cout << "  CHAT <имя_пользователя> - Открыть/показать историю чата с пользователем\n";
        std::cout << "  SEND_PRIVATE <получатель> <сообщение> - Отправить личное сообщение\n";
    }
    std::cout << "  HELP - Показать это сообщение помощи\n";
    std::cout << "  EXIT - ";
    if (isLoggedIn) {
        std::cout << "Выйти из текущей учетной записи\n";
    }
    else {
        std::cout << "Выйти из программы\n";
    }
    std::cout << "-------------------------\n" << std::endl;
}

void displayPrompt() {
    std::cout << "\r" << std::string(100, ' ') << "\r";
    if (G_loggedIn.load()) {
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
    printHelp(G_loggedIn.load());
    displayPrompt();
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
            if (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAESHUTDOWN) {
                return "";
            }
#else
            if (errno == ECONNRESET || errno == EPIPE) {
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
        std::cerr << "\n[СИСТЕМА] Ошибка отправки: " << GET_LAST_ERROR << ". Соединение может быть разорвано." << std::endl;
    }
}

std::string parseUsernameFromWelcome(const std::string& serverResponse) {
    std::string prefix1 = "OK_LOGIN Welcome, ";
    std::string prefix2 = "OK_REGISTERED Welcome, ";
    std::string suffix = "!";
    std::string username;

    size_t startPos = std::string::npos;
    if (serverResponse.rfind(prefix1, 0) == 0) {
        startPos = prefix1.length();
    }
    else if (serverResponse.rfind(prefix2, 0) == 0) {
        startPos = prefix2.length();
    }

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
    bool receiving_history = false;
    std::string history_chat_partner;

    while (G_clientRunning.load()) {
        if (G_programShouldExit.load()) break;

        if (G_clientSocket == INVALID_SOCKET_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        FD_ZERO(&readSet);
        FD_SET(G_clientSocket, &readSet);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int selectNfds = G_clientSocket + 1;
#ifdef _WIN32
        selectNfds = 0;
#endif
        int selectResult = select(selectNfds, &readSet, nullptr, nullptr, &timeout);

        if (G_programShouldExit.load()) break;
        if (!G_clientRunning.load() && !G_programShouldExit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (selectResult < 0) {
            int error_code = GET_LAST_ERROR;
#ifdef _WIN32
            if (error_code == WSAEINTR || error_code == WSAENOTSOCK) {
#else
            if (error_code == EINTR || error_code == EBADF) {
#endif
                if (G_clientRunning.load()) {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    clearConsoleScreen();
                    std::cerr << "\n[ПРИЕМНИК] Ошибка сокета/сокет закрыт (" << error_code << "). Попробуйте перезапустить клиент." << std::endl;
                    std::cout << "Нажмите Enter для выхода из программы..." << std::flush;
                }
            }
            else {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                clearConsoleScreen();
                std::cerr << "\n[ПРИЕМНИК] select ошибка " << error_code << ". Попробуйте перезапустить клиент." << std::endl;
                std::cout << "Нажмите Enter для выхода из программы..." << std::flush;
            }
            G_loggedIn = false;
            G_currentUsername.clear();
            if (G_clientSocket != INVALID_SOCKET_VALUE) {
                CLOSE_SOCKET(G_clientSocket);
                G_clientSocket = INVALID_SOCKET_VALUE;
            }
            G_programShouldExit = true;
            G_clientRunning = false;
            break;
            }

        if (selectResult > 0 && FD_ISSET(G_clientSocket, &readSet)) {
            std::string message = clientReadLine(G_clientSocket);

            std::lock_guard<std::mutex> lock(G_coutMutex);
            if (G_programShouldExit.load() && message.empty()) break;

            if (message.empty() && G_clientRunning.load()) {
                displayPrompt();
                std::cout << "\n[ПРИЕМНИК] Сервер отключился или ошибка чтения." << std::endl;
                G_loggedIn = false;
                G_currentUsername.clear();
                if (G_clientSocket != INVALID_SOCKET_VALUE) {
                    CLOSE_SOCKET(G_clientSocket);
                    G_clientSocket = INVALID_SOCKET_VALUE;
                }
            }
            else if (!message.empty()) {
                displayPrompt();

                std::string prefix;
                std::string payload;
                size_t space_pos = message.find(' ');
                if (space_pos != std::string::npos) {
                    prefix = message.substr(0, space_pos);
                    payload = message.substr(space_pos + 1);
                }
                else {
                    prefix = message;
                }

                if (prefix == "HISTORY_START") {
                    receiving_history = true;
                    history_chat_partner = payload;
                    clearConsoleScreen();
                    std::cout << "--- История чата с " << history_chat_partner << " ---\n" << std::endl;
                }
                else if (prefix == "HIST_MSG") {
                    if (receiving_history) {
                        std::string ts, sender, msg_text;
                        std::istringstream hist_iss(payload); // payload это "timestamp:sender:message_text"
                        std::getline(hist_iss, ts, ':');
                        std::getline(hist_iss, sender, ':');
                        std::getline(hist_iss, msg_text);

                        std::cout << "[" << ts << "] ";
                        if (sender == G_currentUsername) {
                            std::cout << "Вы: ";
                        }
                        else {
                            std::cout << sender << ": ";
                        }
                        std::cout << msg_text << std::endl;
                    }
                }
                else if (prefix == "HISTORY_END") {
                    if (receiving_history && payload == history_chat_partner) {
                        std::cout << "\n--- Конец истории чата с " << history_chat_partner << " ---" << std::endl;
                        receiving_history = false;
                        history_chat_partner.clear();
                    }
                }
                else if (prefix == "NO_HISTORY") {
                    std::cout << "[СИСТЕМА] История сообщений с пользователем '" << payload << "' не найдена." << std::endl;
                }
                else if (message.rfind("OK_LOGIN", 0) == 0 || message.rfind("OK_REGISTERED", 0) == 0) {
                    G_loggedIn = true;
                    G_currentUsername = parseUsernameFromWelcome(message);
                    if (G_currentUsername.empty()) G_currentUsername = "Пользователь";

                    std::cout << "<< " << message << std::endl;
                    std::cout << "\nВы успешно вошли как " << G_currentUsername << "!" << std::endl;
                    std::cout << "Для отправки сообщения используйте: SEND_PRIVATE <кому> <текст сообщения>" << std::endl;
                    std::cout << "Для просмотра истории: CHAT <имя_пользователя>" << std::endl;
                    std::cout << "Для списка команд введите HELP." << std::endl;

                }
                else if (!G_currentUsername.empty() && message.rfind("OK_LOGOUT Goodbye, " + G_currentUsername, 0) == 0) {
                    std::cout << "<< " << message << std::endl;
                    G_loggedIn = false;
                    G_currentUsername.clear();
                }
                else if (message.rfind("ERROR_", 0) == 0) {
                    std::cout << "[ОТВЕТ СЕРВЕРА] " << message << std::endl;
                }
                else if (prefix == "MSG_FROM") {
                    std::cout << "<< " << message << std::endl; // payload здесь "user: text"
                }
                else {
                    std::cout << "[ОТВЕТ СЕРВЕРА] " << message << std::endl;
                }

                if (G_clientRunning.load() && !G_programShouldExit.load()) {
                    displayPrompt();
                }
            }
        }
        }
    {
        std::lock_guard<std::mutex> lock(G_coutMutex);
        std::cout << "\n[ПРИЕМНИК] Поток приема сообщений завершен." << std::endl;
    }
    }

int main() {
#ifdef _WIN32
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
#endif

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[СИСТЕМА] WSAStartup не удался." << std::endl;
        return 1;
    }
#endif

    while (!G_programShouldExit.load()) {

        G_clientRunning = true;
        G_loggedIn = false;
        G_currentUsername.clear();

        if (G_clientSocket == INVALID_SOCKET_VALUE) {
            G_clientSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (G_clientSocket == INVALID_SOCKET_VALUE) {
                std::cerr << "[СИСТЕМА] Создание сокета не удалось: " << GET_LAST_ERROR << std::endl;
#ifdef _WIN32
                WSACleanup();
#endif
                return 1;
            }

            sockaddr_in serverAddress;
            serverAddress.sin_family = AF_INET;
            serverAddress.sin_port = htons(8081);
            const char* server_ip = "127.0.0.1";
#ifdef _WIN32
            if (inet_pton(AF_INET, server_ip, &serverAddress.sin_addr) <= 0) {
                std::cerr << "[СИСТЕМА] inet_pton не удался." << std::endl;
                CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE;
                WSACleanup(); return 1;
            }
#else
            if (inet_aton(server_ip, &serverAddress.sin_addr) == 0) {
                std::cerr << "[СИСТЕМА] inet_aton не удался для " << server_ip << std::endl;
                CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE;
                return 1;
            }
#endif
            {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                std::cout << "[СИСТЕМА] Попытка подключения к серверу " << server_ip << ":8081..." << std::endl;
            }
            if (connect(G_clientSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR_VALUE) {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                clearConsoleScreen();
                std::cerr << "[СИСТЕМА] Подключение к серверу не удалось: " << GET_LAST_ERROR << std::endl;
                std::cerr << "Пожалуйста, проверьте, запущен ли сервер." << std::endl;
                std::cerr << "Нажмите Enter для попытки переподключения или введите EXIT для выхода." << std::endl;
                CLOSE_SOCKET(G_clientSocket); G_clientSocket = INVALID_SOCKET_VALUE;

                std::string temp_input;
                std::getline(std::cin, temp_input);
                std::string temp_input_upper = temp_input;
                std::transform(temp_input_upper.begin(), temp_input_upper.end(), temp_input_upper.begin(),
                    [](unsigned char c) { return std::toupper(c); });
                if (temp_input_upper == "EXIT") {
                    G_programShouldExit = true;
                }
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                std::cout << "[СИСТЕМА] Успешно подключено к серверу." << std::endl;
            }
        }

        std::thread receiverThread;
        if (!G_programShouldExit.load() && G_clientSocket != INVALID_SOCKET_VALUE) {
            receiverThread = std::thread(receiveMessagesThreadFunc);
        }
        else if (G_programShouldExit.load()) {
            break;
        }

        std::string lineInput;

        printInitialScreen();

        while (G_clientRunning.load() && !G_programShouldExit.load()) {
            if (!std::getline(std::cin, lineInput)) {
                if (std::cin.eof()) {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    displayPrompt();
                    std::cout << "\n[СИСТЕМА] Обнаружен конец ввода (EOF). Выход из программы." << std::endl;
                }
                else {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    displayPrompt();
                    std::cout << "\n[СИСТЕМА] Ошибка потока ввода. Выход из программы." << std::endl;
                }
                G_programShouldExit = true;
                G_clientRunning = false;
                break;
            }

            if (!G_clientRunning.load() || G_programShouldExit.load()) break;

            std::string command_token_from_input;
            std::string command_args_from_input;

            size_t first_space_pos = lineInput.find(' ');
            if (first_space_pos != std::string::npos) {
                command_token_from_input = lineInput.substr(0, first_space_pos);
                if (first_space_pos + 1 < lineInput.length()) {
                    command_args_from_input = lineInput.substr(first_space_pos + 1);
                }
            }
            else {
                command_token_from_input = lineInput;
            }

            std::string command_token_upper = command_token_from_input;
            std::transform(command_token_upper.begin(), command_token_upper.end(), command_token_upper.begin(),
                [](unsigned char c) { return ::toupper(c); });

            if (lineInput.empty()) {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                displayPrompt();
                continue;
            }

            bool perform_logout_flow = false;

            if (command_token_upper == "EXIT") {
                if (G_loggedIn.load()) {
                    if (G_clientSocket != INVALID_SOCKET_VALUE) clientSendMessage(G_clientSocket, "LOGOUT");
                    perform_logout_flow = true;
                }
                else {
                    G_programShouldExit = true;
                    G_clientRunning = false;
                }
            }
            else if (command_token_upper == "HELP") {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                printHelp(G_loggedIn.load());
                displayPrompt();
            }
            else if (command_token_upper == "CHAT") {
                if (!G_loggedIn.load()) {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    std::cout << "[СИСТЕМА] Сначала войдите в систему, чтобы просматривать чаты." << std::endl;
                    displayPrompt();
                }
                else if (command_args_from_input.empty()) {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    std::cout << "[СИСТЕМА] Укажите имя пользователя: CHAT <имя_пользователя>" << std::endl;
                    displayPrompt();
                }
                else {
                    if (G_clientSocket != INVALID_SOCKET_VALUE) {
                        clientSendMessage(G_clientSocket, "GET_HISTORY " + command_args_from_input);
                        // Ответ будет обработан в receiverThread
                    }
                    else {
                        std::lock_guard<std::mutex> lock(G_coutMutex);
                        std::cout << "[СИСТЕМА] Нет соединения с сервером." << std::endl;
                        displayPrompt();
                    }
                }
            }
            else if (command_token_upper == "LOGIN" ||
                command_token_upper == "REGISTRATION" ||
                command_token_upper == "SEND_PRIVATE") {

                std::string message_to_send_to_server = command_token_upper;
                if (!command_args_from_input.empty()) {
                    message_to_send_to_server += " " + command_args_from_input;
                }

                if (G_clientSocket == INVALID_SOCKET_VALUE) {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    std::cout << "[СИСТЕМА] Нет активного соединения с сервером. Попробуйте перезапустить клиент или введите EXIT." << std::endl;
                    displayPrompt();
                }
                else {
                    clientSendMessage(G_clientSocket, message_to_send_to_server);
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    displayPrompt();
                }
            }
            else {
                std::lock_guard<std::mutex> lock(G_coutMutex);
                std::cout << "[СИСТЕМА] Неизвестная команда: '" << lineInput << "'. Введите HELP для списка команд." << std::endl;
                displayPrompt();
            }

            if (perform_logout_flow) {
                auto logout_start_time = std::chrono::steady_clock::now();
                bool logged_out_by_receiver = false;
                while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - logout_start_time).count() < 700) {
                    if (!G_loggedIn.load()) {
                        logged_out_by_receiver = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                if (!logged_out_by_receiver) {
                    std::lock_guard<std::mutex> lock(G_coutMutex);
                    G_loggedIn = false;
                    G_currentUsername.clear();
                }
                G_clientRunning = false;
            }
        }

        G_clientRunning = false;
        if (receiverThread.joinable()) {
            receiverThread.join();
        }

        if (G_programShouldExit.load()) {
            if (G_clientSocket != INVALID_SOCKET_VALUE) {
                CLOSE_SOCKET(G_clientSocket);
                G_clientSocket = INVALID_SOCKET_VALUE;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(G_coutMutex);
        std::cout << "\r" << std::string(100, ' ') << "\r";
        std::cout << "[СИСТЕМА] Завершение работы клиента..." << std::endl;
    }

#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[СИСТЕМА] Клиент завершил работу. До новых встреч!" << std::endl;
    return 0;
}