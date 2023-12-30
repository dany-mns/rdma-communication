#include <unistd.h>

#include <arpa/inet.h>
#include <infiniband/verbs.h>

#include <cerrno>
#include <iostream>
#include <string>

#include <boost/program_options.hpp>

using namespace std;

struct device_info
{
	union ibv_gid gid;
	uint32_t send_qp_num, write_qp_num;
	struct ibv_mr write_mr;
};

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

	if ((bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		return 1;

	if ((listen(sockfd, 5)) != 0)
		return 1;

	connfd = accept(sockfd, NULL, NULL); 
	if (connfd < 0)
		return 1;

	read(connfd, &data, sizeof(data));

	close(sockfd);

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

	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
		return 1;

	write(sockfd, &data, sizeof(data));

	close(sockfd);

	return 0;
}

int main(int argc, char *argv[])
{
	bool server = false;
	int num_devices, ret;
	uint32_t gidIndex = 0;
	string ip_str, remote_ip_str, dev_str;
	char data_send[100], data_write[100];

	struct ibv_context *context;
	struct ibv_pd *pd;
	struct ibv_cq *send_cq, *write_cq;
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_qp *send_qp, *write_qp;
	struct ibv_qp_attr qp_attr;
	struct ibv_port_attr port_attr;
	struct device_info local, remote;
	struct ibv_gid_entry gidEntries[255];
	struct ibv_sge sg_send, sg_write, sg_recv;
	struct ibv_send_wr wr_send, *bad_wr_send, wr_write, *bad_wr_write;
	struct ibv_recv_wr wr_recv, *bad_wr_recv;
	struct ibv_mr *send_mr, *write_mr, remote_write_mr;
	struct ibv_wc wc;

	auto flags = IBV_ACCESS_LOCAL_WRITE | 
	             IBV_ACCESS_REMOTE_WRITE | 
	             IBV_ACCESS_REMOTE_READ;

	boost::program_options::options_description desc("Allowed options");
	desc.add_options()
		("help", "show possible options")
		("dev", boost::program_options::value<string>(), "rdma device to use")
		("src_ip", boost::program_options::value<string>(), "source ip")
		("dst_ip", boost::program_options::value<string>(), "destination ip")
		("server", "run as server")
	;

	boost::program_options::variables_map vm;
	boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
	boost::program_options::notify(vm);

	if (vm.count("help"))
	{
		cout << desc << endl;
		return 0;
	}

	if (vm.count("dev"))
		dev_str = vm["dev"].as<string>();
	else
		cerr << "the --dev argument is required" << endl;

	if (vm.count("src_ip"))
		ip_str = vm["src_ip"].as<string>();
	else
		cerr << "the --src_ip argument is required" << endl;

	if (vm.count("dst_ip"))
		remote_ip_str = vm["dst_ip"].as<string>();
	else
		cerr << "the --dst_ip argument is required" << endl;

	if (vm.count("server"))
		server = true;

	// populate dev_list using ibv_get_device_list - use num_devices as argument
	struct ibv_device** dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list)
	{
		cerr << "ibv_get_device_list failed: " << strerror(errno) << endl;
		return 1;
	}

	for (int i = 0; i < num_devices; i++)
	{
		// get the device name, using ibv_get_device_name
		auto dev = ibv_get_device_name(dev_list[i]);
		if (!dev)
		{
			cerr << "ibv_get_device_name failed: " << strerror(errno) << endl;
			goto free_devlist;
		}

		// compare it to the device provided in the program arguments (dev_str)
		// and open the device; store the device context in "context"
		if (strcmp(dev, dev_str.c_str()) == 0)
		{
			context = ibv_open_device(dev_list[i]);
			break;
		}
	}

	// allocate a PD (protection domain), using ibv_alloc_pd
	pd = ibv_alloc_pd(context);
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

	// create a CQ for the write operations, using ibv_create_cq 
	write_cq = ibv_create_cq(context, 0x10, nullptr, nullptr, 0);
	if (!write_cq)
	{
		cerr << "ibv_create_cq - recv - failed: " << strerror(errno) << endl;
		goto free_send_cq;
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
		goto free_write_cq;
	}

	qp_init_attr.recv_cq = write_cq;
	qp_init_attr.send_cq = write_cq;

	// create a QP for the write operations, using ibv_create_qp
	write_qp = ibv_create_qp(pd, &qp_init_attr);
	if (!write_qp)
	{
		cerr << "ibv_create_qp failed: " << strerror(errno) << endl;
		goto free_send_qp;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.qp_state   = ibv_qp_state::IBV_QPS_INIT;
	qp_attr.port_num   = 1;
	qp_attr.pkey_index = 0;
	qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
	                          IBV_ACCESS_REMOTE_WRITE | 
	                          IBV_ACCESS_REMOTE_READ;

	// move both QPs in the INIT state, using ibv_modify_qp 
	ret = ibv_modify_qp(send_qp, &qp_attr,
						IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
	if (ret != 0)
	{
		cerr << "ibv_modify_qp - INIT - failed: " << strerror(ret) << endl;
		goto free_write_qp;
	}

	ret = ibv_modify_qp(write_qp, &qp_attr,
						IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
	if (ret != 0)
	{
		cerr << "ibv_modify_qp - INIT - failed: " << strerror(ret) << endl;
		goto free_write_qp;
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
			gidIndex = entry.gid_index;
			memcpy(&local.gid, &entry.gid, sizeof(local.gid));
			break;
		}
	}

	// GID index 0 should never be used
	if (gidIndex == 0)
	{
		cerr << "Given IP not found in GID table" << endl;
		goto free_write_qp;
	}

	// register the "data_send" and "data_write" buffers for RDMA operations, using ibv_reg_mr;
	// store the resulting mrs in send_mr and write_mr
	send_mr = ibv_reg_mr(pd, data_send, sizeof(data_send), flags);
	if (!send_mr)
	{
		cerr << "ibv_reg_mr failed: " << strerror(errno) << endl;
		goto free_write_qp;
	}

	write_mr = ibv_reg_mr(pd, data_write, sizeof(data_write), flags);
	if (!write_mr)
	{
		cerr << "ibv_reg_mr failed: " << strerror(errno) << endl;
		goto free_send_mr;
	}

	memcpy(&local.write_mr, write_mr, sizeof(local.write_mr));
	local.send_qp_num = send_qp->qp_num;
	local.write_qp_num = write_qp->qp_num;

	// exchange data between the 2 applications
	if(server)
	{
		ret = receive_data(remote);
		if (ret != 0)
		{
			cerr << "receive_data failed: " << endl;
			goto free_write_mr;
		}

		ret = send_data(local, remote_ip_str);
		if (ret != 0)
		{
			cerr << "send_data failed: " << endl;
			goto free_write_mr;
		}
	}
	else
	{
		ret = send_data(local, remote_ip_str);
		if (ret != 0)
		{
			cerr << "send_data failed: " << endl;
			goto free_write_mr;
		}

		ret = receive_data(remote);
		if (ret != 0)
		{
			cerr << "receive_data failed: " << endl;
			goto free_write_mr;
		}
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
		goto free_write_mr;
	}

	qp_attr.dest_qp_num  = remote.write_qp_num;

	// move the write QP into the RTR state, using ibv_modify_qp
	ret = ibv_modify_qp(write_qp, &qp_attr, IBV_QP_STATE | IBV_QP_AV |
						IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
						IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);

	if (ret != 0)
	{
		cerr << "ibv_modify_qp - RTR - failed: " << strerror(ret) << endl;
		goto free_write_mr;
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
		goto free_write_mr;
	}

	ret = ibv_modify_qp(write_qp, &qp_attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
						IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
	if (ret != 0)
	{
		cerr << "ibv_modify_qp - RTS - failed: " << strerror(ret) << endl;
		goto free_write_mr;
	}

	memset(data_send, 0, sizeof(data_send));
	memset(data_write, 0, sizeof(data_write));

	if (server)
	{
		memcpy(data_write, "Hello, but with write", 21);

		// initialise sg_write with the write mr address, size and lkey
		memset(&sg_write, 0, sizeof(sg_write));
		sg_write.addr   = (uintptr_t)write_mr->addr;
		sg_write.length = sizeof(data_write);
		sg_write.lkey   = write_mr->lkey;
		
		// create a work request, with the Write With Immediate operation
		memset(&wr_write, 0, sizeof(wr_write));
		wr_write.wr_id      = 0;
		wr_write.sg_list    = &sg_write;
		wr_write.num_sge    = 1;
		wr_write.opcode     = IBV_WR_RDMA_WRITE_WITH_IMM;
		wr_write.send_flags = IBV_SEND_SIGNALED;

		wr_write.imm_data            = htonl(0x1234);

		// fill the wr.rdma field of wr_write with the remote address and key
		wr_write.wr.rdma.remote_addr = (uintptr_t)remote.write_mr.addr;
		wr_write.wr.rdma.rkey        = remote.write_mr.rkey;

		// post the work request, using ibv_post_send
		ret = ibv_post_send(write_qp, &wr_write, &bad_wr_write);
		if (ret != 0)
		{
			cerr << "ibv_post_send failed: " << strerror(ret) << endl;
			goto free_write_mr;
		}

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

		// post the receive work request, using ibv_post_recv, for the send QP
		ret = ibv_post_recv(send_qp, &wr_recv, &bad_wr_recv);
		if (ret != 0)
		{
			cerr << "ibv_post_recv failed: " << strerror(ret) << endl;
			goto free_write_mr;
		}

		// poll send_cq, using ibv_poll_cq, until it returns different than 0
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
			goto free_write_mr;
		}

		cout << data_send << endl;
	}
	else
	{
		memcpy(data_send, "Hello", 5);

		// initialise sg_write with the write mr address, size and lkey
		memset(&sg_recv, 0, sizeof(sg_recv));
		sg_recv.addr   = (uintptr_t)write_mr->addr;
		sg_recv.length = sizeof(data_write);
		sg_recv.lkey   = write_mr->lkey;

		memset(&wr_recv, 0, sizeof(wr_recv));
		wr_recv.wr_id      = 0;
		wr_recv.sg_list    = &sg_recv;
		wr_recv.num_sge    = 1;

		// post a receive work request, using ibv_post_recv, for the write QP
		ret = ibv_post_recv(write_qp, &wr_recv, &bad_wr_recv);
		if (ret != 0)
		{
			cerr << "ibv_post_recv failed: " << strerror(ret) << endl;
			goto free_write_mr;
		}

		// poll write_cq, using ibv_poll_cq, until it returns different than 0
		ret = 0;
		do
		{
			ret = ibv_poll_cq(write_cq, 1, &wc);
		} while (ret == 0);

		// check the wc (work completion) structure status;
		//         return error on anything different than ibv_wc_status::IBV_WC_SUCCESS
		if (wc.status != ibv_wc_status::IBV_WC_SUCCESS)
		{
			cerr << "ibv_poll_cq failed: " << ibv_wc_status_str(wc.status) << endl;
			goto free_write_mr;
		}

		cout << data_write << endl;

		// initialise sg_send with the send mr address, size and lkey
		memset(&sg_send, 0, sizeof(sg_send));
		sg_send.addr   = (uintptr_t)send_mr->addr;
		sg_send.length = sizeof(data_send);
		sg_send.lkey   = send_mr->lkey;

		// create a work request, with the RDMA Send operation
		memset(&wr_send, 0, sizeof(wr_send));
		wr_send.wr_id      = 0;
		wr_send.sg_list    = &sg_send;
		wr_send.num_sge    = 1;
		wr_send.opcode     = IBV_WR_SEND;
		wr_send.send_flags = IBV_SEND_SIGNALED;

		// post the work request, using ibv_post_send
		ret = ibv_post_send(send_qp, &wr_send, &bad_wr_send);
		if (ret != 0)
		{
			cerr << "ibv_post_recv failed: " << strerror(ret) << endl;
			goto free_write_mr;
		}
	}

free_write_mr:
	// free write_mr, using ibv_dereg_mr
	ibv_dereg_mr(write_mr);

free_send_mr:
	// free send_mr, using ibv_dereg_mr
	ibv_dereg_mr(send_mr);

free_write_qp:
	// free write_qp, using ibv_destroy_qp
	ibv_destroy_qp(write_qp);

free_send_qp:
	// free send_qp, using ibv_destroy_qp
	ibv_destroy_qp(send_qp);

free_write_cq:
	// free write_cq, using ibv_destroy_cq
	ibv_destroy_cq(write_cq);

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
