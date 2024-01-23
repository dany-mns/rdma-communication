#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <infiniband/verbs.h>

const char* SERVER_IP = "192.168.1.132";
const int PORT = 8080;

struct device_info
{
	union ibv_gid gid;
	uint32_t send_qp_num;
};


int main() {
    int clientSocket;
    struct sockaddr_in serverAddr;

    // Create socket
    if ((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        return 1;
    }

    // Set up server address structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Connect to the server
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Connection failed");
        close(clientSocket);
        return 1;
    }

    // Send a message to the server
    const char* message = "Hello, server!";
    if (send(clientSocket, message, strlen(message), 0) == -1) {
        perror("Message sending failed");
    } else {
        std::cout << "Message sent to the server: " << message << std::endl;
    }

    // Close the client socket
    close(clientSocket);

    return 0;
}
