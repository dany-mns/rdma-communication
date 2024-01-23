#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <list>
#include <fcntl.h> 

#include <infiniband/verbs.h>
using namespace std;

struct device_info
{
	union ibv_gid gid;
	uint32_t send_qp_num;
};
const int PORT = 8080;
const int BUFFER_SIZE = 1024;

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

struct ibv_qp *create_qp_for_send(struct ibv_qp_init_attr &qp_init_attr, struct ibv_pd *pd, struct ibv_cq *send_cq) {
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.recv_cq = send_cq;
	qp_init_attr.send_cq = send_cq;
	qp_init_attr.qp_type    = IBV_QPT_RC;
	qp_init_attr.sq_sig_all = 1;
	qp_init_attr.cap.max_send_wr  = 5;
	qp_init_attr.cap.max_recv_wr  = 5;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	// create a QP (queue pair) for the send operations, using ibv_create_qp
	return ibv_create_qp(pd, &qp_init_attr);
}
