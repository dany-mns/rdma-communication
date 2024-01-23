#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <infiniband/verbs.h>
#include "common.h"

using namespace std;

const char* SERVER_IP = "192.168.1.132";

int main() {
    int clientSocket;
    struct sockaddr_in serverAddr;
    ssize_t bytesRead;
    const char* message = "[CLIENT] RDMA device info about client";


    // ==== RDMA variables ====
    struct device_info local_rdma, server_rdma;
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
		goto free_context;
	}

	send_cq = ibv_create_cq(context, 0x10, nullptr, nullptr, 0);
	if (!send_cq)
	{
		cerr << "ibv_create_cq - send - failed: " << strerror(errno) << endl;
		goto free_pd;
	}

	send_qp = create_qp_for_send(qp_init_attr, pd, send_cq);
	if (!send_qp)
	{
		cerr << "ibv_create_qp failed: " << strerror(errno) << endl;
		goto free_send_cq;
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
		goto free_send_qp;
	}

    send_mr = ibv_reg_mr(pd, data_send, sizeof(data_send), IBV_ACCESS_LOCAL_WRITE | 
	             IBV_ACCESS_REMOTE_WRITE | 
	             IBV_ACCESS_REMOTE_READ);
	if (!send_mr)
	{
		cerr << "ibv_reg_mr failed: " << strerror(errno) << endl;
		goto free_send_qp;
	}

	local_rdma.send_qp_num = send_qp->qp_num;


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
    cout << "> Send RDMA device info to MASTER" << endl;
    if (send(clientSocket, &local_rdma, sizeof(local_rdma), 0) == -1) {
        perror("Message sending failed");
    }

    bytesRead = recv(clientSocket, &server_rdma, sizeof(server_rdma), 0);

    if (bytesRead == -1) {
        perror("Error while receiving data");
    } else if (bytesRead == 0) {
        cout << "Server disconnected." << endl;
    } else {
        // Null-terminate the received data to treat it as a string
        cout << "> Receive RDMA device info from MASTER. QP: " << server_rdma.send_qp_num << ", intf: " << server_rdma.gid.global.interface_id << endl;
    }

    char buffer[BUFFER_SIZE];
    while(true) {
        cout << "Waiting for UNLOCK from MASTER" << endl;
        bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

        if (bytesRead == -1) {
            perror("Error while receiving data");
            break;
        } else if (bytesRead == 0) {
            cout << "Server disconnected." << endl;
            break;
        } else {
            buffer[bytesRead] = '\0';
            cout << "Received message from server: " << buffer << endl;
            // Send data using RDMA
            // check if i have data

            cout << "Node unblocked to send data" << endl;
            cout << "Checking if I have data to send to master node ..." << endl;

            cout << "Send data through RDMA" << endl;
        }
    }

free_send_qp:
	ibv_destroy_qp(send_qp);

free_send_cq:
	ibv_destroy_cq(send_cq);

free_pd:
	ibv_dealloc_pd(pd);
    
free_context:
	ibv_close_device(context);

free_devlist:
	ibv_free_device_list(dev_list);

close_socket:
    close(clientSocket);
	
    return 0;
}
