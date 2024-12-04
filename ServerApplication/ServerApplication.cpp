#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <functional>
#include <future>
#include <condition_variable>
#include <winsock2.h>
#include <ws2tcpip.h>

const int THREAD_POOL_SIZE = 8;

class ThreadPool {
private:
    std::vector<std::thread> workers;                // Worker threads
    std::queue<std::function<void()>> tasks;         // Task queue
    std::mutex queueMutex;                           // Mutex for queue
    std::condition_variable condition;              // Condition variable
    bool stop;                                       // Flag to stop threads

public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;

                    // Wait for tasks
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this]() { return stop || !tasks.empty(); });

                        if (stop && tasks.empty())
                            return;

                        task = std::move(tasks.front());
                        tasks.pop();
                    }

                    // Execute the task
                    task();
                }
                });
        }
    }

    // Submit a task to the thread pool
    template <typename Func, typename... Args>
    auto submit(Func&& func, Args&&... args) -> std::future<typename std::result_of<Func(Args...)>::type> {
        using ReturnType = typename std::result_of<Func(Args...)>::type;

        // Wrap the function and its arguments into a packaged_task
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

        std::future<ReturnType> result = task->get_future();

        // Add the task to the queue
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            tasks.emplace([task]() { (*task)(); });
        }

        condition.notify_one();
        return result;
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers)
            worker.join();
    }
};

void handleClient(SOCKET clientSocket) {
    char buffer[1024];
    while (true) {
        // Clear the buffer
        ZeroMemory(buffer, sizeof(buffer));

        // Receive data from the client
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);  // Leave space for null terminator
        if (bytesReceived <= 0) {
            // If no data or error, assume client disconnected
            std::cout << "Client disconnected.\n";
            break;  // Exit the loop to close the socket
        }

        // Null terminate the received data to treat it as a string
        buffer[bytesReceived] = '\0';

        std::cout << "Server received: " << buffer << std::endl;  // Print received data

        // Send response back to client
        std::string response = "Server received: " + std::string(buffer);
        send(clientSocket, response.c_str(), response.size(), 0);
    }

    // Once done, close the socket
    closesocket(clientSocket);
}


int main() {
    /* Create WSAS */
    SOCKET serverSocket, acceptSocket;
    int port = 55555;
    WSADATA wsaData;
    int wsaerr;
    WORD wVersionRequested = MAKEWORD(2, 2);
    wsaerr = WSAStartup(wVersionRequested, &wsaData);

    if (wsaerr != 0) {
        std::cout << "Winsock dll not found! \n";
        return 0;
    }
    else {
        std::cout << "Winsock dll found\n";
        std::cout << "Status: " << wsaData.szSystemStatus << "\n";
    }

    /* Create the socket */
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (serverSocket == INVALID_SOCKET) {
        std::cout << "Error at socket(): " << WSAGetLastError() << "\n";
        WSACleanup();
        return 0;
    }
    else {
        std::cout << "Socket is OK!" << "\n";
    }

    /* Binding the socket to the IP and Port */
    sockaddr_in service;
    LPCWSTR wide_ip = L"127.0.0.1";
    service.sin_family = AF_INET;
    InetPton(AF_INET, wide_ip, &service.sin_addr.s_addr);
    service.sin_port = htons(port);
    if (bind(serverSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        std::cout << "bind() Failed: " << WSAGetLastError() << "\n";
        closesocket(serverSocket);
        WSACleanup();
        return 0;
    }
    else {
        std::cout << "bind() is OK!\n";
    }

    /* Listen for the socket */
    if (listen(serverSocket, 1) == SOCKET_ERROR) {
        std::cout << "listen() Error listening on socket " << WSAGetLastError() << "\n";
        closesocket(serverSocket);
        WSACleanup();
        return 0;
    }
    else {
        std::cout << "listen() on port: " << port << " is OK, waiting for connections..." << "\n";
    }

    ThreadPool pool(THREAD_POOL_SIZE); // Thread pool with multiple threads

    while (true) {
        /* Accept the socket (looped for multiple clients) */
        acceptSocket = accept(serverSocket, NULL, NULL);
        if (acceptSocket == INVALID_SOCKET) {
            std::cout << "accept() failed: " << WSAGetLastError() << "\n";
            WSACleanup();
            return -1;
        }

        std::cout << "Accepted Connection\n";
        // Launch a new thread from the pool to handle the client
        std::cout << "Thread Used from the ThreadPool to handle the Client\n";
        pool.submit(handleClient, acceptSocket);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
