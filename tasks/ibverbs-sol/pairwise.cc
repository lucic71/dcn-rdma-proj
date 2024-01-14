#include <fcntl.h>
#include <math.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <map>

#include <boost/program_options.hpp>

using namespace std; 

int myrank;
std::map<std::string, int> pipes;

ssize_t readall(int fd, void *buff, size_t nbyte) {
	size_t nread = 0; size_t res = 0;
	char *cbuff = (char *) buff;
	while (nread < nbyte) {
		res = read(fd, cbuff+nread, nbyte-nread);
		if (res == 0)
		{
			cout << "[pairwise] read returned 0" << std::endl;
			break;
		}
		if (res == -1)
		{
			cout << "[pairwise] error read: " << strerror(errno) << std::endl;
			return -1;
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
		if (res == 0)
		{
			cout << "[pairwise] write returned 0" << std::endl;
			break;
		}
		if (res == -1)
		{
			cout << "[pairwise] error write: " << strerror(errno) << std::endl;
			return -1;
		}
		nwrote += res;
	}
	return nwrote;
}

ssize_t rread(int rank, void *buff, size_t nbyte) {
	std::string pipe = "/tmp/pipe-" + std::to_string(rank) + "-" + std::to_string(myrank);
	int pipefd = pipes[pipe];
	std::cout << "[pairwise] will read from " << pipe << " at fd " << pipefd << "\n";
	return readall(pipefd, buff, nbyte);	
}

ssize_t rwrite(int rank, void *buff, size_t nbyte) {
	std::string pipe = "/tmp/pipe-" + std::to_string(myrank) + "-" + std::to_string(rank);
	int pipefd = pipes[pipe];
	return writeall(pipefd, buff, nbyte);	
}

int alltoall_pairwise(const void* sendbuf,
        const int entries_per_cell,
        void* recvbuf,
        int rank,
        int num_procs, int bytes_per_entry)
{
    int write_proc, read_proc;
    int send_pos, recv_pos;
    int ret;

    char* recv_buffer = (char*)recvbuf;
    char* send_buffer = (char*)sendbuf;

	std::cout << "[pairwise] recv_buffer at beginning: ";
	for (int i = 0; i < bytes_per_entry * entries_per_cell * num_procs; i++)
		std::cout << (int) recv_buffer[i] << " ";
	std::cout << std::endl;

	std::cout << "[pairwise] send_buffer at beginning: ";
	for (int i = 0; i < bytes_per_entry * entries_per_cell * num_procs; i++)
		std::cout << (int) send_buffer[i] << " ";
	std::cout << std::endl;

    // Send to rank + i
    // Recv from rank - i
    for (int i = 1; i < num_procs; i++)
    {
	std::cout << "[pairwise] in loop, i: " << i << std::endl;
	int size = entries_per_cell * bytes_per_entry;

        write_proc = rank + i;
        if (write_proc >= num_procs)
            write_proc -= num_procs;
        read_proc = rank - i;
        if (read_proc < 0)
            read_proc += num_procs;

        send_pos = write_proc * size;
        recv_pos = read_proc * size;
	
	std::cout << "[pairwise] will write to rank " << write_proc << " at offset " << send_pos << " in send_buffer, with size " << size << "\n";
        ret = rwrite(write_proc, send_buffer + send_pos, size);
	if (ret != size) 
	{
		cout << "rwrite only wrote " << ret << " bytes: " << strerror(ret) << endl;
		exit(-1);
	}

	std::cout << "[pairwise] will read from rank " << read_proc << " at offset " << recv_pos << " in recv_buffer, with size " << size << "\n";
        ret = rread(read_proc, recv_buffer + recv_pos, size);
	if (ret != size)
	{
		cout << "rread only read " << ret << " bytes: " << strerror(ret) << endl;
		exit(-1);
	}

	std::cout << "[pairwise] recv_buffer at the end of the loop: ";
	for (int i = 0; i < bytes_per_entry * entries_per_cell * num_procs; i++)
		std::cout << (int) recv_buffer[i] << " ";
	std::cout << std::endl;
    }

    return 0;
}

std::map<std::string, int> open_pipes(int num_procs)
{
	std::map<std::string, int> map;
	std::string pipe_wr, pipe_rd;
	int fd;
	for (int i = 0; i < num_procs; i++) {
		if (i == myrank) 
			continue;

		pipe_rd = "/tmp/pipe-" + std::to_string(i) + "-" + std::to_string(myrank);
		pipe_wr = "/tmp/pipe-" + std::to_string(myrank) + "-" + std::to_string(i);

		fd = open(pipe_wr.c_str(), O_WRONLY);
		if (fd == -1)
		{
			cout << "[bruck] open error on pipe_wr " << pipe_wr << ": " << strerror(errno) << endl;
			exit(-1);
		}
		map[pipe_wr] = fd;

		std::cout << "[pairwise] Added " << fd << " at " << pipe_wr << "\n";

		fd = open(pipe_rd.c_str(), O_RDONLY);
		if (fd == -1)
		{
			cout << "[bruck] open error on pipe_rd " << pipe_rd << ": " << strerror(errno) << endl;
			exit(-1);
		}
		map[pipe_rd] = fd;

		std::cout << "[pairwise] Added " << fd << " at " << pipe_rd << "\n";
	}
	return map;
}

bool close_pipes(std::map<std::string, int> map)
{
	sleep(10);
	for (auto const& e : map) {
		if (close(e.second) == -1)
		{
			cout << "[bruck] close error on pipe " << e.first << ": " << strerror(errno) << endl;
			false;
		}
	}
	return true;
}

int main(int argc, char *argv[])
{
	int num_procs, entries_per_cell;
	int *rbuf, *sbuf;

	boost::program_options::options_description desc("Allowed options");
	desc.add_options()
		("help", "show possible options")
		("rank", boost::program_options::value<int>(), "rank")
		("num_procs", boost::program_options::value<int>(), "num_procs")
		("entries_per_cell", boost::program_options::value<int>(), "entries_per_cell")
	;

	boost::program_options::variables_map vm;
	boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
	boost::program_options::notify(vm);

	if (vm.count("rank"))
		myrank = vm["rank"].as<int>();
	else
	{
		cout << "the --rank argument is required" << endl;
		return -1;
	}

	if (vm.count("num_procs"))
		num_procs = vm["num_procs"].as<int>();
	else
	{
		cout << "the --num_procs argument is required" << endl;
		return -1;
	}

	if (vm.count("entries_per_cell"))
		entries_per_cell = vm["entries_per_cell"].as<int>();
	else
	{
		cout << "the --entries_per_cell argument is required" << endl;
		return -1;
	}

	pipes = open_pipes(num_procs);

	rbuf = (int *) malloc(sizeof(int) * entries_per_cell * num_procs);
	if (!rbuf)
	{
		cout << "malloc failed: " << strerror(errno) << endl;
		return -1;
	}

	memset(rbuf, 0xff, sizeof(int) * entries_per_cell * num_procs);
	for (int i = myrank * entries_per_cell; i < (myrank + 1) * entries_per_cell; i++)
		rbuf[i] = myrank;

	sbuf = (int *) malloc(sizeof(int) * entries_per_cell * num_procs);
	if (!sbuf)
	{
		cout << "malloc failed: " << strerror(errno) << endl;
		return -1;
	}

	for (int i = 0; i < entries_per_cell * num_procs; i++)
		sbuf[i] = myrank;

	std::cout << "Initial data: ";
	for (int i = 0; i < entries_per_cell * num_procs; i++)
		std::cout << sbuf[i] << " ";
	std::cout << std::endl;

	alltoall_pairwise(sbuf, entries_per_cell, rbuf, myrank, num_procs, sizeof(int));

	std::cout << "Final data: ";
	for (int i = 0; i < entries_per_cell * num_procs; i++)
		std::cout << rbuf[i] << " ";
	std::cout << std::endl;

	close_pipes(pipes);

	return 0;
}
