#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <infiniband/verbs.h>
using namespace std;

const char* SERVER_IP = "192.168.1.132";
const int PORT = 8080;
const int BUFFER_SIZE = 1024;

struct device_info
{
	union ibv_gid gid;
	uint32_t send_qp_num;
};

bool startsWith(const char* fullString, const char* prefix) {
    return strncmp(fullString, prefix, strlen(prefix)) == 0;
}


void set_gid(struct ibv_context *context, struct ibv_port_attr &port_attr, struct device_info *local) {
    int port = 1;
	struct ibv_gid_entry gidEntries[255];

	ibv_query_port(context, port, &port_attr);
	ibv_query_gid_table(context, gidEntries, port_attr.gid_tbl_len, 0);

	for (auto &entry : gidEntries)
	{
		// we want only RoCEv2
		if (entry.gid_type != IBV_GID_TYPE_ROCE_V2)
			continue;

		in6_addr addr;
		memcpy(&addr, &entry.gid.global, sizeof(addr));
		
		char interface_id[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &addr, interface_id, INET6_ADDRSTRLEN);

		uint32_t ip;
		inet_pton(AF_INET, interface_id + strlen("::ffff:"), &ip);

		if (startsWith(interface_id + strlen("::ffff:"), "192.168"))
		{
			memcpy(&local->gid, &entry.gid, sizeof(local->gid));
			break;
		}
	}

}

struct ibv_device** get_rxe_device() {
	int num_devices;
	struct ibv_device** dev_list = ibv_get_device_list(&num_devices);
	cout << "Found " << num_devices << " device(s)" << endl;
	if (!dev_list || num_devices != 1)
	{
		cerr << "ibv_get_device_list failed or number of devices != 1" << strerror(errno) << endl;
		exit(1);
	}
	cout << "Interface to use: " << dev_list[0]->name << " more info found at: " << dev_list[0]->ibdev_path << endl;
	return dev_list;
}

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
    struct ibv_port_attr port_attr;

    set_gid(context, port_attr, &local_rdma);
	
    if (!pd)
	{
		cerr << "ibv_alloc_pd failed: " << strerror(errno) << endl;
		goto free_context;
	}


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
    if (send(clientSocket, message, strlen(message), 0) == -1) {
        perror("Message sending failed");
    }


    // Read from server where can I send some data
    char buffer[BUFFER_SIZE];
    bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

    if (bytesRead == -1) {
        perror("Error while receiving data");
    } else if (bytesRead == 0) {
        cout << "Server disconnected." << endl;
    } else {
        // Null-terminate the received data to treat it as a string
        buffer[bytesRead] = '\0';
        cout << "> Receive RDMA device info from MASTER " << buffer << endl;
    }


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


    
free_context:
	ibv_close_device(context);

free_devlist:
	ibv_free_device_list(dev_list);

close_socket:
    close(clientSocket);
	
    return 0;
}
