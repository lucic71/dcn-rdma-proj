#include <fcntl.h>
#include <math.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

using namespace std; 

int myrank;

void rotate(void* recvbuf,
        int new_first_byte,
        int last_byte)
{
    char* recv_buffer = (char*)(recvbuf);
    std::rotate(recv_buffer, &(recv_buffer[new_first_byte]), &(recv_buffer[last_byte]));
} 

ssize_t readall(int fd, void *buff, size_t nbyte) {
	size_t nread = 0; size_t res = 0;
	char *cbuff = (char *) buff;
	while (nread < nbyte) {
		res = read(fd, cbuff+nread, nbyte-nread);
		if (res == 0) break;
		if (res == -1)
		{
			cout << "error read: " << strerror(errno) << std::endl;
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
		if (res == 0) break;
		if (res == -1)
		{
			cout << "error write: " << strerror(errno) << std::endl;
			return -1;
		}
		nwrote += res;
	}
	return nwrote;
}

ssize_t rread(int rank, void *buff, size_t nbyte) {
	std::string pipe = "/tmp/pipe-" + std::to_string(rank) + "-" + std::to_string(myrank);
	int pipefd = open(pipe.c_str(), O_RDONLY);
	int ret;

	if (pipefd == -1)
	{
		cout << "[bruck] rread open failed on " << pipe << ": " << strerror(errno) << endl;
		return -1;
	}

	std::cout << "[bruck] before readall\n";
	ret = readall(pipefd, buff, nbyte);	

	if (close(pipefd) == -1)
	{
		cout << "close failed: " << strerror(errno) << endl;
		return -1;
	}

	return ret;
}

ssize_t rwrite(int rank, void *buff, size_t nbyte) {
	std::string pipe = "/tmp/pipe-" + std::to_string(myrank) + "-" + std::to_string(rank);
	int pipefd = open(pipe.c_str(), O_WRONLY);
	int ret;

	if (pipefd == -1)
	{
		cout << "[bruck] rwrite  open failed on " << pipe << ": " << strerror(errno) << endl;
		return -1;
	}

	ret = writeall(pipefd, buff, nbyte);	

	if (close(pipefd) == -1)
	{
		cout << "close failed: " << strerror(errno) << endl;
		return -1;
	}

	return ret;
}

int alltoall_bruck(const void* sendbuf,
        const int entries_per_cell,
        void* recvbuf,
        int rank, int num_procs, int bytes_per_entry)
{
    char* recv_buffer = (char*)recvbuf;
    ssize_t ret;

    std::cout << "initial recv_buffer: ";
    for (int i = 0; i < entries_per_cell*bytes_per_entry*num_procs; i++)
        std::cout << (int) recv_buffer[i] << ", ";
    std::cout << std::endl;

    if (sendbuf != recvbuf) {
        std::cout << "sendbuf != recvbuf, doing a memcpy\n";
        memcpy(recvbuf, sendbuf, entries_per_cell*bytes_per_entry*num_procs);
    }

    std::cout << "recv_buffer after memcpy: ";
    for (int i = 0; i < entries_per_cell*bytes_per_entry*num_procs; i++)
        std::cout << (int) recv_buffer[i] << ", ";
    std::cout << std::endl;

    // Perform all-to-all
    int stride, ctr, group_size;
    int write_proc, read_proc, size;
    int num_steps = log2(num_procs);
    int msg_size = entries_per_cell*bytes_per_entry;
    int total_count = entries_per_cell*num_procs;

    // TODO : could have only half this size
    char* contig_buf = (char*)malloc(total_count*bytes_per_entry);
    char* tmpbuf = (char*)malloc(total_count*bytes_per_entry);

    // 1. rotate local data
    if (rank) {
        std::cout << "rotating the recv_buffer\n";
        rotate(recv_buffer, rank*msg_size, num_procs*msg_size);
    }

    std::cout << "recv_buffer after rotate: ";
    for (int i = 0; i < entries_per_cell*bytes_per_entry*num_procs; i++)
        std::cout << (int) recv_buffer[i] << ", ";
    std::cout << std::endl;

    // 2. send to left, recv from right
    stride = 1;
    for (int i = 0; i < num_steps; i++)
    {
        std::cout << "enter loop, stride: " << stride << endl;

        read_proc = rank - stride;
        if (read_proc < 0) read_proc += num_procs;
        write_proc = rank + stride;
        if (write_proc >= num_procs) write_proc -= num_procs;

        std::cout << "read_proc: " << read_proc << ", write_proc: " << write_proc << endl;

        group_size = stride * entries_per_cell;
        
        ctr = 0;
        for (int i = group_size; i < total_count; i += (group_size*2))
        {
            for (int j = 0; j < group_size; j++)
            {
                for (int k = 0; k < bytes_per_entry; k++)
                {
                    std::cout << "contig_buf[" << ctr*bytes_per_entry+k << "] takes value: " << (int) recv_buffer[(i+j)*bytes_per_entry+k] << ", from index: " << (i+j)*bytes_per_entry+k << std::endl;
                    contig_buf[ctr*bytes_per_entry+k] = recv_buffer[(i+j)*bytes_per_entry+k];
                }
                ctr++;
            }
        }

        size = ((int)(total_count / group_size) * group_size) / 2;
	size *= entries_per_cell*bytes_per_entry;

        std::cout << "contig_buf: ";
        for (int i = 0; i < size; i++)
            std::cout << (int) contig_buf[i] << " ";
        std::cout << std::endl;

        ret = rwrite(write_proc, contig_buf, size);
	if (ret != size) 
	{
		cout << "rwrite only wrote " << ret << " bytes: " << strerror(ret) << endl;
		exit(-1);
	}
        std::cout << "rwrite ok" << std::endl;

        ret = rread(read_proc, tmpbuf, size);
	if (ret != size)
	{
		cout << "rread only read " << ret << " bytes: " << strerror(ret) << endl;
		exit(-1);
	}
        std::cout << "rread ok" << std::endl;

        std::cout << "recv_buffer before processing: ";
        for (int i = 0; i < size; i++)
       	    std::cout << (int) recv_buffer[i] << ", ";
        std::cout << std::endl;

        ctr = 0;
        for (int i = group_size; i < total_count; i += (group_size*2))
        {
            for (int j = 0; j < group_size; j++)
            {
                for (int k = 0; k < bytes_per_entry; k++)
                {
                    recv_buffer[(i+j)*bytes_per_entry+k] = tmpbuf[ctr*bytes_per_entry+k];
                }
                ctr++;
            }
        }

        std::cout << "recv_buffer after processing: ";
        for (int i = 0; i < size; i++)
       	    std::cout << (int) recv_buffer[i] << ", ";
        std::cout << std::endl;

        stride *= 2;
    } 

    // 3. rotate local data
    if (rank < num_procs) {
        std::cout << "final rotate of recv_buffer\n";
        rotate(recv_buffer, (rank+1)*msg_size, num_procs*msg_size);
    }

    std::cout << "final recv_buffer: ";
    for (int i = 0; i < entries_per_cell*bytes_per_entry*num_procs; i++)
        std::cout << (int) recv_buffer[i] << ", ";
    std::cout << std::endl;


    // TODO :: REVERSE!

    return 0;
}

int main(int argc, char *argv[])
{
	int num_procs;
	int *rbuf, *sbuf;

	boost::program_options::options_description desc("Allowed options");
	desc.add_options()
		("help", "show possible options")
		("rank", boost::program_options::value<int>(), "rank")
		("num_procs", boost::program_options::value<int>(), "num_procs")
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

	rbuf = (int *) malloc(sizeof(int) * num_procs);
	if (!rbuf)
	{
		cout << "malloc failed: " << strerror(errno) << endl;
		return -1;
	}

	sbuf = (int *) malloc(sizeof(int) * num_procs);
	if (!sbuf)
	{
		cout << "malloc failed: " << strerror(errno) << endl;
		return -1;
	}

	for (int i = 0; i < num_procs; i++)
		sbuf[i] = myrank;

	std::cout << "Initial data: ";
	for (int i = 0; i < num_procs; i++)
		std::cout << sbuf[i] << " ";
	std::cout << std::endl;

	alltoall_bruck(sbuf, 1, rbuf, myrank, num_procs, sizeof(int));

	std::cout << "Final data: ";
	for (int i = 0; i < num_procs; i++)
		std::cout << rbuf[i] << " ";
	std::cout << std::endl;

	return 0;
}
