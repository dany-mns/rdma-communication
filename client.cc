#include <unistd.h>

#include <arpa/inet.h>
#include <infiniband/verbs.h>

#include <cerrno>
#include <iostream>
#include <string>
#include <cstring>

#include <boost/program_options.hpp>

using namespace std;

struct device_info
{
	union ibv_gid gid;
	uint32_t send_qp_num;
};

int receive_data(struct device_info &data);
int send_data(const struct device_info &data, string ip);

int main(int argc, char *argv[])
{
	int num_devices, ret;
	uint32_t gidIndex = 0;
	string ip_str, remote_ip_str;
	char data_send[100];
	const char* data_to_send = "Hello from server with send operation";

	struct ibv_cq *send_cq;
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp *send_qp;
	struct ibv_qp_attr qp_attr;
	struct ibv_port_attr port_attr;
	struct device_info local, remote;
	struct ibv_gid_entry gidEntries[255];
	struct ibv_sge sg_send, sg_write, sg_recv;
	struct ibv_send_wr wr_send, *bad_wr_send, wr_write, *bad_wr_write;
	struct ibv_recv_wr wr_recv, *bad_wr_recv;
	struct ibv_mr *send_mr;
	struct ibv_wc wc;

	auto flags = IBV_ACCESS_LOCAL_WRITE | 
	             IBV_ACCESS_REMOTE_WRITE | 
	             IBV_ACCESS_REMOTE_READ;

	boost::program_options::options_description desc("Allowed options");
	desc.add_options()
		("help", "show possible options")
		("src_ip", boost::program_options::value<string>(), "source ip")
		("dst_ip", boost::program_options::value<string>(), "destination ip")
	;

	boost::program_options::variables_map vm;
	boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
	boost::program_options::notify(vm);

	if (vm.count("help"))
	{
		cout << desc << endl;
		return 0;
	}

	if (vm.count("src_ip"))
		ip_str = vm["src_ip"].as<string>();
	else
		cerr << "the --src_ip argument is required" << endl;

	if (vm.count("dst_ip"))
		remote_ip_str = vm["dst_ip"].as<string>();
	else
		cerr << "the --dst_ip argument is required" << endl;

	// populate dev_list using ibv_get_device_list - use num_devices as argument
	struct ibv_device** dev_list = ibv_get_device_list(&num_devices);
	cout << "Found " << num_devices << " device(s)" << endl;
	if (!dev_list || num_devices != 1)
	{
		cerr << "ibv_get_device_list failed or number of devices != 1" << strerror(errno) << endl;
		return 1;
	}
	cout << "Interface to use: " << dev_list[0]->name << " more info found at: " << dev_list[0]->ibdev_path << endl;
	struct ibv_context *context = ibv_open_device(dev_list[0]);
	struct ibv_pd *pd = ibv_alloc_pd(context);
	int qp_attrs_flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
	if (!pd)
	{
		cerr << "ibv_alloc_pd failed: " << strerror(errno) << endl;
		goto free_context;
	}

	// create a CQ (completion queue) for the send operations, using ibv_create_cq 
	send_cq = ibv_create_cq(context, 0x10, nullptr, nullptr, 0);
	if (!send_cq)
	{
		cerr << "ibv_create_cq - send - failed: " << strerror(errno) << endl;
		goto free_pd;
	}

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
	send_qp = ibv_create_qp(pd, &qp_init_attr);
	if (!send_qp)
	{
		cerr << "ibv_create_qp failed: " << strerror(errno) << endl;
		goto free_send_cq;
	}
	local.send_qp_num = send_qp->qp_num;

	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.qp_state   = ibv_qp_state::IBV_QPS_INIT;
	qp_attr.port_num   = 1;
	qp_attr.pkey_index = 0;
	qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
	                          IBV_ACCESS_REMOTE_WRITE | 
	                          IBV_ACCESS_REMOTE_READ;

	// move both QPs in the INIT state, using ibv_modify_qp 
	ret = ibv_modify_qp(send_qp, &qp_attr, qp_attrs_flags);
	if (ret != 0)
	{
		cerr << "ibv_modify_qp - INIT - failed: " << strerror(ret) << endl;
		goto free_send_qp;
	}

	// use ibv_query_port to get information about port number 1
	ibv_query_port(context, 1, &port_attr);

	// fill gidEntries with the GID table entries of the port, using ibv_query_gid_table
	ibv_query_gid_table(context, gidEntries, port_attr.gid_tbl_len, 0);

	for (auto &entry : gidEntries)
	{
		// we want only RoCEv2
		if (entry.gid_type != IBV_GID_TYPE_ROCE_V2)
			continue;

		// take the IPv4 address from each entry, and compare it with the supplied source IP address
		in6_addr addr;
		memcpy(&addr, &entry.gid.global, sizeof(addr));
		
		char interface_id[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &addr, interface_id, INET6_ADDRSTRLEN);

		uint32_t ip;
		inet_pton(AF_INET, interface_id + strlen("::ffff:"), &ip);

		if (strncmp(ip_str.c_str(), interface_id + strlen("::ffff:"), INET_ADDRSTRLEN) == 0)
		{
            cout << "Found GID: " << entry.gid_index << endl;
			gidIndex = entry.gid_index;
			memcpy(&local.gid, &entry.gid, sizeof(local.gid));
			break;
		}
	}

	// GID index 0 should never be used
	if (gidIndex == 0)
	{
		cerr << "Given IP not found in GID table" << endl;
		goto free_pd;
	}

	send_mr = ibv_reg_mr(pd, data_send, sizeof(data_send), flags);
	if (!send_mr)
	{
		cerr << "ibv_reg_mr failed: " << strerror(errno) << endl;
		goto free_send_qp;
	}


	// exchange data between the 2 applications
	ret = send_data(local, remote_ip_str);
	if (ret != 0)
	{
		cerr << "send_data failed: " << endl;
		goto free_send_mr;
	}

	ret = receive_data(remote);
	if (ret != 0)
	{
		cerr << "receive_data failed: " << endl;
		goto free_send_mr;
	}

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

	memcpy(&qp_attr.ah_attr.grh.dgid, &remote.gid, sizeof(remote.gid));

	qp_attr.ah_attr.grh.flow_label    = 0;
	qp_attr.ah_attr.grh.hop_limit     = 5;
	qp_attr.ah_attr.grh.sgid_index    = gidIndex;
	qp_attr.ah_attr.grh.traffic_class = 0;

	qp_attr.ah_attr.dlid = 1;
	qp_attr.dest_qp_num  = remote.send_qp_num;

	// move the send QP into the RTR state, using ibv_modify_qp
	ret = ibv_modify_qp(send_qp, &qp_attr, IBV_QP_STATE | IBV_QP_AV |
						IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
						IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);

	if (ret != 0)
	{
		cerr << "ibv_modify_qp - RTR - failed: " << strerror(ret) << endl;
		goto free_send_mr;
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
        goto free_send_mr;
    }

    cout << "Pooling for data..." << endl;
    ret = 0;
    do
    {
        ret = ibv_poll_cq(send_cq, 1, &wc);
    } while (ret == 0);

    // check the wc (work completion) structure status;
    // return error on anything different than ibv_wc_status::IBV_WC_SUCCESS
    if (wc.status != ibv_wc_status::IBV_WC_SUCCESS)
    {
        cerr << "ibv_poll_cq failed: " << ibv_wc_status_str(wc.status) << endl;
        goto free_send_mr;
    }

	cout << "Done receive data '" << data_send << "'" << endl; 

free_send_mr:
	// free send_mr, using ibv_dereg_mr
	ibv_dereg_mr(send_mr);

free_send_qp:
	// free send_qp, using ibv_destroy_qp
	ibv_destroy_qp(send_qp);

free_send_cq:
	// free send_cq, using ibv_destroy_cq
	ibv_destroy_cq(send_cq);

free_pd:
	// free pd, using ibv_dealloc_pd
	ibv_dealloc_pd(pd);

free_context:
	// close the RDMA device, using ibv_close_device
	ibv_close_device(context);

free_devlist:
	// free dev_list, using ibv_free_device_list
	ibv_free_device_list(dev_list);

	return 0;
}

int receive_data(struct device_info &data)
{
	int sockfd, connfd, len; 
	struct sockaddr_in servaddr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1)
		return 1;

	memset(&servaddr, 0, sizeof(servaddr)); 

	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	servaddr.sin_port = htons(8080); 

	cout << "Start receive data for ip: " << servaddr.sin_addr.s_addr << endl;

	if ((bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		return 1;

	if ((listen(sockfd, 5)) != 0)
		return 1;

	connfd = accept(sockfd, NULL, NULL); 
	if (connfd < 0)
		return 1;

	read(connfd, &data, sizeof(data));
	close(sockfd);

	cout << "RECEIVE: send_qp_num: " << data.send_qp_num << endl;

	return 0;
}

int send_data(const struct device_info &data, string ip)
{
	int sockfd; 
	struct sockaddr_in servaddr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1)
		return 1;

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(ip.c_str());
	servaddr.sin_port = htons(8080);

	cout << "SEND: ip: " << ip.c_str() << " with send_qp_num: " << data.send_qp_num << endl;
	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
		return 1;

	write(sockfd, &data, sizeof(data));
	close(sockfd);

	return 0;
}
