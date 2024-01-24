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
typedef struct rdma_client_ {
    int socket_fd;
    struct device_info rdma_info;
} rdma_client_s;

list<rdma_client_s> clients;

// RDMA params
struct device_info local_rdma;
struct ibv_qp_attr qp_attr;
uint32_t gidIndex = 0;
struct ibv_port_attr port_attr;
struct ibv_qp *send_qp;
struct ibv_recv_wr wr_recv, *bad_wr_recv;
struct ibv_sge sg_send, sg_write, sg_recv;
struct ibv_mr *send_mr;
struct ibv_cq *send_cq;
struct ibv_wc wc;

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

    rdma_client_s client_rdma_info;
    client_rdma_info.socket_fd = clientSocket;
    client_rdma_info.rdma_info = client_rdma;
    // TODO add a check in case same client reconnect
    clients.push_back(client_rdma_info);
    set_socket_non_blocking(clientSocket);
}


void set_attr_for_reset_state(struct ibv_qp_attr &qp_attr) {
	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.qp_state   = ibv_qp_state::IBV_QPS_RESET;
	qp_attr.port_num   = 1;
	qp_attr.pkey_index = 0;
	qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
	                          IBV_ACCESS_REMOTE_WRITE | 
	                          IBV_ACCESS_REMOTE_READ;
}

void set_attr_for_init_state(struct ibv_qp_attr &qp_attr) {
	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.qp_state   = ibv_qp_state::IBV_QPS_INIT;
	qp_attr.port_num   = 1;
	qp_attr.pkey_index = 0;
	qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
	                          IBV_ACCESS_REMOTE_WRITE | 
	                          IBV_ACCESS_REMOTE_READ;
}

void set_attr_for_rtr_state(struct ibv_qp_attr &qp_attr, rdma_client_s client) {
    memset(&qp_attr, 0, sizeof(qp_attr));

    qp_attr.path_mtu              = port_attr.active_mtu;
    qp_attr.qp_state              = ibv_qp_state::IBV_QPS_RTR;
    qp_attr.rq_psn                = 0;
    qp_attr.max_dest_rd_atomic    = 1;
    qp_attr.min_rnr_timer         = 0;
    qp_attr.ah_attr.is_global     = 1;
    qp_attr.ah_attr.sl            = 0;
    qp_attr.ah_attr.src_path_bits = 0;
    qp_attr.ah_attr.port_num      = 1;

    memcpy(&qp_attr.ah_attr.grh.dgid, &client.rdma_info.gid, sizeof(client.rdma_info.gid));

    qp_attr.ah_attr.grh.flow_label    = 0;
    qp_attr.ah_attr.grh.hop_limit     = 5;
    qp_attr.ah_attr.grh.sgid_index    = gidIndex;
    qp_attr.ah_attr.grh.traffic_class = 0;

    qp_attr.ah_attr.dlid = 1;
    qp_attr.dest_qp_num  = client.rdma_info.send_qp_num;
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
        exit(1);
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
        close(client.socket_fd);
    }

    clients.clear();
}

void rdma_communication() {
    cout << "START RDMA COMMUNICATION" << endl;
    char data_send[100];
    int previous_client_socket = -1;
    int ret;
    while(true) {
        for(const auto &client : clients) {
            cout << "> Unlock client socket: " << client.socket_fd << " to send data" << endl;
            const char* unlockMessage = "[SERVER] UNLOCK";
            ssize_t bytesSent = send(client.socket_fd, unlockMessage, strlen(unlockMessage), 0);

            if (bytesSent == -1) {
                perror("Error while sending data");
            } else {
                std::cout << "Unlock successfully sent to client (Socket " << client.socket_fd << "): " << unlockMessage << std::endl;
            }

            // Pool data from RDMA for a small period of time
            cout << "Pool for data from queue for client " << client.socket_fd << endl;


            // EXPERIMENT

            // set_attr_for_reset_state(qp_attr);
            // ret = ibv_modify_qp(send_qp, &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
            // if (ret != 0)
            // {
            //     cerr << "ibv_modify_qp - RESET - failed: " << strerror(ret) << endl;
            //     exit(1);
            // }

            // set_attr_for_init_state(qp_attr);
            // ret = ibv_modify_qp(send_qp, &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
            // if (ret != 0)
            // {
            //     cerr << "ibv_modify_qp - INIT - failed: " << strerror(ret) << endl;
            //     exit(1);
            // }

            // ==========================


            // set_attr_for_rtr_state(qp_attr, client);

            // // move the send QP into the RTR state, using ibv_modify_qp
            // ret = ibv_modify_qp(send_qp, &qp_attr, IBV_QP_STATE | IBV_QP_AV |
            //                     IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
            //                     IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);

            if (ret != 0)
            {
                cerr << "ibv_modify_qp - RTR - failed: " << strerror(ret) << endl;
                exit(1);
            }

            memset(data_send, 0, sizeof(data_send));

            // initialise sg_send with the send mr address, size and lkey
            memset(&sg_recv, 0, sizeof(sg_recv));
            sg_recv.addr   = (uintptr_t)send_mr->addr;
            sg_recv.length = sizeof(data_send);
            sg_recv.lkey   = send_mr->lkey;

            // create a receive work request
            memset(&wr_recv, 0, sizeof(wr_recv));
            wr_recv.wr_id      = 0;
            wr_recv.sg_list    = &sg_recv;
            wr_recv.num_sge    = 1;

            cout << "Post work request to receive data" << endl;
            ret = ibv_post_recv(send_qp, &wr_recv, &bad_wr_recv);
            if (ret != 0)
            {
                cerr << "ibv_post_recv failed: " << strerror(ret) << endl;
                exit(1);
            }

            cout << "Pooling for data..." << endl;
            ret = 0;
            int number_of_retries = 0;
            do
            {
                ret = ibv_poll_cq(send_cq, 1, &wc);
                number_of_retries++;
            } while (ret == 0 && number_of_retries < 100);

            if (wc.status != ibv_wc_status::IBV_WC_SUCCESS)
            {
                cerr << "ibv_poll_cq failed: " << ibv_wc_status_str(wc.status) << endl;
                exit(1);
            }

            cout << "Done receive data '" << data_send << "'" << endl; 

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
    int ret;
    char data_send[100];

    set_gid(context, port_attr, &local_rdma, gidIndex);
	
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


    set_attr_for_init_state(qp_attr);
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
