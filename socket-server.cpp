#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include "common.h"
using namespace std;

const int BACKLOG = 5;
list<int> clients;

struct device_info local_rdma;

void handleClient(int clientSocket) {
    struct device_info client_rdma;
    
    ssize_t bytesRead = recv(clientSocket, &client_rdma, sizeof(client_rdma), 0);

    if (bytesRead == -1) {
        perror("Error while receiving data");
        return;
    } else if (bytesRead == 0) {
        std::cout << "Client disconnected. Client socket: " << clientSocket << std::endl;
        return;
    } else {
        // Null-terminate the received data to treat it as a string
        cout << "> Receive RDMA device info from NODE. QP: " << client_rdma.send_qp_num << ", intf: " << client_rdma.gid.global.interface_id <<  endl;

        // std::cout << "Received message from client (Socket " << clientSocket << "): " << buffer << std::endl;
        
        cout << "> Send RDMA device info to NODE. QP: " << local_rdma.send_qp_num << endl;
        ssize_t bytesSent = send(clientSocket, &local_rdma, sizeof(local_rdma), 0);

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

    // ==== RDMA variables ====
    struct ibv_device** dev_list = get_rxe_device();
	struct ibv_context *context = ibv_open_device(dev_list[0]);
	struct ibv_pd *pd = ibv_alloc_pd(context);
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_port_attr port_attr;
    struct ibv_qp *send_qp;
    struct ibv_cq *send_cq;
    int ret;
    struct ibv_qp_attr qp_attr;
    struct ibv_mr *send_mr;
    char data_send[100];

    set_gid(context, port_attr, &local_rdma);
	
    if (!pd)
	{
		cerr << "ibv_alloc_pd failed: " << strerror(errno) << endl;
		exit(1);
	}

	send_cq = ibv_create_cq(context, 0x10, nullptr, nullptr, 0);
	if (!send_cq)
	{
		cerr << "ibv_create_cq - send - failed: " << strerror(errno) << endl;
		exit(1);
	}

	send_qp = create_qp_for_send(qp_init_attr, pd, send_cq);
	if (!send_qp)
	{
		cerr << "ibv_create_qp failed: " << strerror(errno) << endl;
		exit(1);
	}


	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.qp_state   = ibv_qp_state::IBV_QPS_INIT;
	qp_attr.port_num   = 1;
	qp_attr.pkey_index = 0;
	qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
	                          IBV_ACCESS_REMOTE_WRITE | 
	                          IBV_ACCESS_REMOTE_READ;

	// move both QPs in the INIT state, using ibv_modify_qp 
	ret = ibv_modify_qp(send_qp, &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
	if (ret != 0)
	{
		cerr << "ibv_modify_qp - INIT - failed: " << strerror(ret) << endl;
		exit(1);
	}

    send_mr = ibv_reg_mr(pd, data_send, sizeof(data_send), IBV_ACCESS_LOCAL_WRITE | 
	             IBV_ACCESS_REMOTE_WRITE | 
	             IBV_ACCESS_REMOTE_READ);
	if (!send_mr)
	{
		cerr << "ibv_reg_mr failed: " << strerror(errno) << endl;
		exit(1);
	}

	local_rdma.send_qp_num = send_qp->qp_num;


    std::thread serverThread(acceptConnections);

    std::thread rdma_communication_thread(rdma_communication);

    serverThread.join();
    rdma_communication_thread.join();


    cout << "End of main!" << endl;
    return 0;
}
