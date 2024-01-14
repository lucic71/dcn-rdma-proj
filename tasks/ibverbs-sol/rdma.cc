#include <unistd.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <infiniband/verbs.h>

#include <cerrno>
#include <iostream>
#include <fstream>
#include <string>

#include <boost/program_options.hpp>

using namespace std;

struct device_info
{
	union ibv_gid gid;
	uint32_t send_qp_num, write_qp_num;
	struct ibv_mr write_mr;
};

//int port = 9210;
int port;

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
	servaddr.sin_port = htons(port); 

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

ssize_t readall(int fd, void *buff, size_t nbyte) {
	size_t nread = 0; size_t res = 0;
	char *cbuff = (char *) buff;
	while (nread < nbyte) {
		res = read(fd, cbuff+nread, nbyte-nread);
		if (res == 0)
		{
			cerr << "[rdma-" << port << "] read ret 0\n";
			break;
		}
		if (res == -1)
		{
			cerr << "[rdma-" << port << "] error read\n";
			exit(-1);
		}
		nread += res;
	}
	return nread;
}

ssize_t writeall(int fd, void *buff, size_t nbyte) {
	size_t nwrote = 0; size_t res = 0;
	char *cbuff = (char *) buff;
	while (nwrote < nbyte) {
		res = write(fd, cbuff+nwrote, nbyte-nwrote);
		if (res == 0) break;
		if (res == -1)
		{
			cerr << "[rdma-" << port << "] error write\n";
			exit(-1);
		}
		nwrote += res;
	}
	return nwrote;
}


int send_data(const struct device_info &data, string ip)
{
	int sockfd; 
	int ret;
	struct sockaddr_in servaddr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1)
	{
		cerr << "[rdma-" << port << "] error socket\n";
		return 1;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(ip.c_str());
	servaddr.sin_port = htons(port);

	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
	{
		cerr << "[rdma-" << port << "] error connect: " << strerror(errno) << endl;
		return 1;
	}

	writeall(sockfd, (void *) &data, sizeof(data));

	close(sockfd);

	return 0;
}

int main(int argc, char *argv[])
{
	bool server = false;
	int num_devices, ret;
	uint32_t gidIndex = 0;
	string ip_str, remote_ip_str, dev_str;
	char shared_buf[1000];
	std::string pipe;
	int pipefd;
	int datasize;

	struct ibv_device **dev_list;
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
		("pipe", boost::program_options::value<string>(), "pipe")
		("datasize", boost::program_options::value<int>(), "datasize")
		("port", boost::program_options::value<int>(), "port")
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
		cerr << "[rdma-" << port << "] the --dev argument is required" << endl;

	if (vm.count("datasize"))
		datasize = vm["datasize"].as<int>();
	else
		cerr << "[rdma-" << port << "] the --datasize argument is required" << endl;

	if (vm.count("port"))
		port = vm["port"].as<int>();
	else
		cerr << "[rdma-" << port << "] the --port argument is required" << endl;

	if (vm.count("pipe"))
		pipe = vm["pipe"].as<string>();
	else
		cerr << "[rdma-" << port << "] the --pipe argument is required" << endl;

	if (vm.count("src_ip"))
		ip_str = vm["src_ip"].as<string>();
	else
		cerr << "[rdma-" << port << "] the --src_ip argument is required" << endl;

	if (vm.count("dst_ip"))
		remote_ip_str = vm["dst_ip"].as<string>();
	else
		cerr << "[rdma-" << port << "] the --dst_ip argument is required" << endl;

	if (vm.count("server"))
		server = true;

	std::cout << "[rdma-" << port << "] parsing args ok\n";

	// populate dev_list using ibv_get_device_list - use num_devices as argument
	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list)
	{
		cerr << "[rdma-" << port << "] ibv_get_device_list failed: " << strerror(errno) << endl;
		return 1;
	}

	for (int i = 0; i < num_devices; i++)
	{
		// get the device name, using ibv_get_device_name
		auto dev = ibv_get_device_name(dev_list[i]);
		if (!dev)
		{
			cerr << "[rdma-" << port << "] ibv_get_device_name failed: " << strerror(errno) << endl;
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
	std::cout << "[rdma-" << port << "] got ibv dev ok\n";

	// allocate a PD (protection domain), using ibv_alloc_pd
	pd = ibv_alloc_pd(context);
	if (!pd)
	{
		cerr << "[rdma-" << port << "] ibv_alloc_pd failed: " << strerror(errno) << endl;
		goto free_context;
	}

	// create a CQ (completion queue) for the send operations, using ibv_create_cq 
	send_cq = ibv_create_cq(context, 0x10, nullptr, nullptr, 0);
	if (!send_cq)
	{
		cerr << "[rdma-" << port << "] ibv_create_cq - send - failed: " << strerror(errno) << endl;
		goto free_pd;
	}

	// create a CQ for the write operations, using ibv_create_cq 
	write_cq = ibv_create_cq(context, 0x10, nullptr, nullptr, 0);
	if (!write_cq)
	{
		cerr << "[rdma-" << port << "] ibv_create_cq - recv - failed: " << strerror(errno) << endl;
		goto free_send_cq;
	}
	std::cout << "[rdma-" << port << "] allocated pd cq ok\n";

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
		cerr << "[rdma-" << port << "] ibv_create_qp failed: " << strerror(errno) << endl;
		goto free_write_cq;
	}

	qp_init_attr.recv_cq = write_cq;
	qp_init_attr.send_cq = write_cq;

	// create a QP for the write operations, using ibv_create_qp
	write_qp = ibv_create_qp(pd, &qp_init_attr);
	if (!write_qp)
	{
		cerr << "[rdma-" << port << "] ibv_create_qp failed: " << strerror(errno) << endl;
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
		cerr << "[rdma-" << port << "] ibv_modify_qp - INIT - failed: " << strerror(ret) << endl;
		goto free_write_qp;
	}

	ret = ibv_modify_qp(write_qp, &qp_attr,
						IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
	if (ret != 0)
	{
		cerr << "[rdma-" << port << "] ibv_modify_qp - INIT - failed: " << strerror(ret) << endl;
		goto free_write_qp;
	}
	std::cout << "[rdma-" << port << "] initialized rdma internal structs ok\n";

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
		cerr << "[rdma-" << port << "] Given IP not found in GID table" << endl;
		goto free_write_qp;
	}
	std::cout << "[rdma-" << port << "] initialized git index ok\n";

	write_mr = ibv_reg_mr(pd, shared_buf, sizeof(shared_buf), flags);
	if (!write_mr)
	{
		cerr << "[rdma-" << port << "] ibv_reg_mr failed: " << strerror(errno) << endl;
		goto free_send_mr;
	}

	memcpy(&local.write_mr, write_mr, sizeof(local.write_mr));
	local.send_qp_num = send_qp->qp_num;
	local.write_qp_num = write_qp->qp_num;
	std::cout << "[rdma-" << port << "] registered buffers ok\n";

	// exchange data between the 2 applications
	if(server)
	{
		ret = receive_data(remote);
		if (ret != 0)
		{
			cerr << "[rdma-" << port << "] receive_data failed: " << endl;
			goto free_write_mr;
		}

		ret = send_data(local, remote_ip_str);
		if (ret != 0)
		{
			cerr << "[rdma-" << port << "] send_data failed: " << endl;
			goto free_write_mr;
		}
	}
	else
	{
		while (1) {
			ret = send_data(local, remote_ip_str);
			if (ret != 0)
			{
				int secs = 5;
				cerr << "[rdma-" << port << "] send_data failed, retrying in secs " << secs << endl;
				sleep(secs);
				continue;
			}

			ret = receive_data(remote);
			if (ret != 0)
			{
				cerr << "[rdma-" << port << "] receive_data failed: " << endl;
				goto free_write_mr;
			}
			break;
		}
	}
	std::cout << "[rdma-" << port << "] send/recv handshake ok\n";

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
		cerr << "[rdma-" << port << "] ibv_modify_qp - RTR - failed: " << strerror(ret) << endl;
		goto free_write_mr;
	}

	qp_attr.dest_qp_num  = remote.write_qp_num;

	// move the write QP into the RTR state, using ibv_modify_qp
	ret = ibv_modify_qp(write_qp, &qp_attr, IBV_QP_STATE | IBV_QP_AV |
						IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
						IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);

	if (ret != 0)
	{
		cerr << "[rdma-" << port << "] ibv_modify_qp - RTR - failed: " << strerror(ret) << endl;
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
		cerr << "[rdma-" << port << "] ibv_modify_qp - RTS - failed: " << strerror(ret) << endl;
		goto free_write_mr;
	}

	ret = ibv_modify_qp(write_qp, &qp_attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
						IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
	if (ret != 0)
	{
		cerr << "[rdma-" << port << "] ibv_modify_qp - RTS - failed: " << strerror(ret) << endl;
		goto free_write_mr;
	}
	std::cout << "[rdma-" << port << "] modified qps ok\n";

	if (server)
		pipefd = open(pipe.c_str(), O_RDONLY);
	else
		pipefd = open(pipe.c_str(), O_WRONLY);

	if (pipefd == -1) {
		cerr << "[rdma-" << port << "] open failed: " << strerror(errno) << endl;
		return 1;
	}
	std::cout << "[rdma-" << port << "] opened pipe ok\n";

	memset(shared_buf, 0x80, sizeof(shared_buf));

	if (server)
	{
		while (1) {
			int ret;

			memset(shared_buf, 0xff, sizeof(shared_buf));

			std::cout << "[rdma-" << port << "] [server] going to read bytes " << datasize << std::endl;
			ret = readall(pipefd, shared_buf, datasize);
			if (ret != datasize)
			{
				cerr << "[rdma-" << port << "] readall only read " << ret << " bytes: " << strerror(ret) << endl;
				goto free_write_mr;
			}
			sleep(2); // TODO: make this smaller

			cout << "[rdma-" << port << "] [server] ret: " << ret << std::endl;
			cout << "[rdma-" << port << "] [server] shared_buf: ";
			for (int i = 0; i < datasize; i++) std::cout << (int) shared_buf[i] << ", ";
			std::cout << "[rdma-" << port << "] \n";

			// initialise sg_write with the write mr address, size and lkey
			memset(&sg_write, 0, sizeof(sg_write));
			sg_write.addr   = (uintptr_t)write_mr->addr;
			sg_write.length = datasize;
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
			errno = 0;
			ret = ibv_post_send(write_qp, &wr_write, &bad_wr_write);
			if (ret != 0)
			{
				cerr << "[rdma-" << port << "] ibv_post_send failed: " << strerror(ret) << endl;
				goto free_write_mr;
			}
			cout << "[rdma-" << port << "] [server] sent data to client\n";
			usleep(50000);
		}
	}
	else
	{
		while (1) {
			int ret;

			// initialise sg_write with the write mr address, size and lkey
			memset(&sg_recv, 0, sizeof(sg_recv));
			sg_recv.addr   = (uintptr_t)write_mr->addr;
			sg_recv.length = datasize;
			sg_recv.lkey   = write_mr->lkey;

			memset(&wr_recv, 0, sizeof(wr_recv));
			wr_recv.wr_id      = 0;
			wr_recv.sg_list    = &sg_recv;
			wr_recv.num_sge    = 1;

			// post a receive work request, using ibv_post_recv, for the write QP
			ret = ibv_post_recv(write_qp, &wr_recv, &bad_wr_recv);
			if (ret != 0)
			{
				cerr << "[rdma-" << port << "] ibv_post_recv failed: " << strerror(ret) << endl;
				goto free_write_mr;
			}

			// poll write_cq, using ibv_poll_cq, until it returns different than 0
			ret = 0;
			do
			{
				std::ifstream donefile("/tmp/done"); bool done;
				donefile >> done;
				if (done) goto free_write_mr;	

				ret = ibv_poll_cq(write_cq, 1, &wc);
			} while (ret == 0);

			// check the wc (work completion) structure status;
			//         return error on anything different than ibv_wc_status::IBV_WC_SUCCESS
			if (wc.status != ibv_wc_status::IBV_WC_SUCCESS)
			{
				cerr << "[rdma-" << port << "] ibv_poll_cq failed: " << ibv_wc_status_str(wc.status) << endl;
				goto free_write_mr;
			}

			cout << "[rdma-" << port << "] [client] shared_buf: ";
			for (int i = 0; i < datasize; i++) std::cout << (int) shared_buf[i] << ", ";
			std::cout << "\n";

			ret = writeall(pipefd, shared_buf, datasize);
			if (ret != datasize)
			{
				cerr << "[rdma-" << port << "] writeall only wrote " << ret << " bytes: " << strerror(ret) << endl;
				goto free_write_mr;
			}
			cout << "[rdma-" << port << "] writeall success\n";
		}
	}

free_write_mr:
	if (server) {
		std::ofstream done("/tmp/done");
		done << 1;
		done.close();
	}

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
