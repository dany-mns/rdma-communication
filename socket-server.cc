#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <list>
#include <fcntl.h> 
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

int set_socket_non_blocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        return -1;
    }

    if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL failed");
        return -1;
    }

    return 0;
}

void handleClient(int clientSocket) {
    char buffer[BUFFER_SIZE];
    
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

    if (bytesRead == -1) {
        perror("Error while receiving data");
        return;
    } else if (bytesRead == 0) {
        std::cout << "Client disconnected. Client socket: " << clientSocket << std::endl;
        return;
    } else {
        // Null-terminate the received data to treat it as a string
        cout << "> Receive RDMA device info from NODE." << endl;

        buffer[bytesRead] = '\0';
        std::cout << "Received message from client (Socket " << clientSocket << "): " << buffer << std::endl;
        
        cout << "< Send RDMA device info to NODE" << endl;
        const char* responseMessage = "[SERVER] RDMA DEVICE INFO";
        ssize_t bytesSent = send(clientSocket, responseMessage, strlen(responseMessage), 0);

        if (bytesSent == -1) {
            perror("Error while sending data");
            return;
        } 
    }

    if(!clientSocketExist(clients, clientSocket)) {
        clients.push_back(clientSocket);
        // Send client in non blocking because we will just send unlock message going further
        set_socket_non_blocking(clientSocket);
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

        // Receive RDMA data when client first connects
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

void rdma_communication() {
    cout << "START RDMA COMMUNICATION" << endl;
    while(true) {
        for(const auto &client : clients) {
            cout << "> Unlock client socket: " << client << " to send data" << endl;
            const char* unlockMessage = "[SERVER] UNLOCK";
            ssize_t bytesSent = send(client, unlockMessage, strlen(unlockMessage), 0);

            if (bytesSent == -1) {
                perror("Error while sending data");
            } else {
                std::cout << "Unlock successfully sent to client (Socket " << client << "): " << unlockMessage << std::endl;
            }

            // Pool data from RDMA for a small period of time
            cout << "Pool for data from queue for client " << client << endl;

            // Sleep before next client TODO Remove later
            cout << "Sleep few seconds" << endl;
            sleep(5);
        }
    }
}

int main() {
    std::thread serverThread(acceptConnections);

    std::thread rdma_communication_thread(rdma_communication);

    serverThread.join();
    rdma_communication_thread.join();


    cout << "End of main!" << endl;
    return 0;
}
