#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <list>
using namespace std;

const int PORT = 8080;
const int BACKLOG = 5;
const int BUFFER_SIZE = 1024;
list<int> clients;

bool clientSocketExist(std::list<int> clients, int currentSocket) {
    for(const int& client : clients) {
        if(client == currentSocket) {
            return true;
        }
    }

    return false;
}

// Function to handle a client connection
void handleClient(int clientSocket) {
    char buffer[BUFFER_SIZE];
    
    // Receive data from the client
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

    if (bytesRead == -1) {
        perror("Error while receiving data");
    } else if (bytesRead == 0) {
        std::cout << "Client disconnected. Client socket: " << clientSocket << std::endl;
    } else {
        // Null-terminate the received data to treat it as a string
        buffer[bytesRead] = '\0';
        std::cout << "Received message from client (Socket " << clientSocket << "): " << buffer << std::endl;
    }
}

// Function to accept incoming client connections
void acceptConnections() {
    int serverSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    // Create socket
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        return;
    }
    cout << "Server socket " << serverSocket << " was created: " << endl;

    // Set up server address structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Binding failed");
        close(serverSocket);
        return;
    }

    // Listen for incoming connections
    if (listen(serverSocket, BACKLOG) == -1) {
        perror("Listening failed");
        close(serverSocket);
        return;
    }

    std::cout << "Server listening on port " << PORT << std::endl;

    while (true) {
        if ((clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen)) == -1) {
            perror("Accepting connection failed");
            continue;
        }

        char ipString[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &(clientAddr.sin_addr), ipString, INET_ADDRSTRLEN) == nullptr) {
            perror("Error converting IP address to string");
            return;
        }

        if(!clientSocketExist(clients, clientSocket)) {
            cout << "Client connected to server with ip: " << ipString << " and port: " << clientAddr.sin_port << endl;
            clients.push_back(clientSocket);
        }

        std::thread clientThread(handleClient, clientSocket);
        clientThread.detach();  // Detach the thread to allow it to run independently
    }

    // Close the server socket (unreachable in this example)
    close(serverSocket);
}

void cleanClientList() {
    for (const auto& client : clients) {
        close(client);
    }

    clients.clear();
}

int main() {
    std::thread serverThread(acceptConnections);

    serverThread.join();

    return 0;
}
