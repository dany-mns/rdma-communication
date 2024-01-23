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
    const char* data_to_send = "Hello from NODE !!!";
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
    struct ibv_sge sg_send;
    struct ibv_send_wr wr_send, *bad_wr_send;
    uint32_t gidIndex = 0;

    set_gid(context, port_attr, &local_rdma, gidIndex);
	
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

	memcpy(&qp_attr.ah_attr.grh.dgid, &server_rdma.gid, sizeof(server_rdma.gid));

	qp_attr.ah_attr.grh.flow_label    = 0;
	qp_attr.ah_attr.grh.hop_limit     = 5;
	qp_attr.ah_attr.grh.sgid_index    = gidIndex;
	qp_attr.ah_attr.grh.traffic_class = 0;

	qp_attr.ah_attr.dlid = 1;
	qp_attr.dest_qp_num  = server_rdma.send_qp_num;

	// move the send QP into the RTR state, using ibv_modify_qp
	ret = ibv_modify_qp(send_qp, &qp_attr, IBV_QP_STATE | IBV_QP_AV |
						IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
						IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);

	if (ret != 0)
	{
		cerr << "ibv_modify_qp - RTR - failed: " << strerror(ret) << endl;
		exit(1);
	}

	qp_attr.qp_state      = ibv_qp_state::IBV_QPS_RTS;
	qp_attr.timeout       = 0;
	qp_attr.retry_cnt     = 7;
	qp_attr.rnr_retry     = 7;
	qp_attr.sq_psn        = 0;
	qp_attr.max_rd_atomic = 0;

	// move the send and write QPs into the RTS state, using ibv_modify_qp
	ret = ibv_modify_qp(send_qp, &qp_attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
						IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
	if (ret != 0)
	{
		cerr << "ibv_modify_qp - RTS - failed: " << strerror(ret) << endl;
		goto free_send_mr;
	}

	memset(data_send, 0, sizeof(data_send));
	memcpy(data_send, data_to_send, strlen(data_to_send));

	// initialise sg_send with the send mr address, size and lkey
	memset(&sg_send, 0, sizeof(sg_send));
	sg_send.addr   = (uintptr_t)send_mr->addr;
	sg_send.length = sizeof(data_send);
	sg_send.lkey   = send_mr->lkey;

	cout << "Using for sending: addr " << (uintptr_t)send_mr->addr << " and lkey: " << send_mr->lkey << endl;

	// create a work request, with the RDMA Send operation
	memset(&wr_send, 0, sizeof(wr_send));
	wr_send.wr_id      = 0;
	wr_send.sg_list    = &sg_send;
	wr_send.num_sge    = 1;
	wr_send.opcode     = IBV_WR_SEND;
	wr_send.send_flags = IBV_SEND_SIGNALED;

    // ====== Create socket =======
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

            // ===== RDMA operation ======
            ret = ibv_post_send(send_qp, &wr_send, &bad_wr_send);
            if (ret != 0)
            {
                cerr << "ibv_post_recv failed: " << strerror(ret) << endl;
            }

            cout << "Done sending data: '" << data_to_send << "' with len: " << strlen(data_to_send) << endl; 
        }
    }

free_send_mr:
	ibv_dereg_mr(send_mr);

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
