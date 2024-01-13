LDFLAGS = -libverbs -lboost_program_options

all: rdma bruck

rdma: rdma.cc
	$(CXX) $^ -o $@ $(LDFLAGS)

bruck: bruck.cc
	$(CXX) $^ -o $@ $(LDFLAGS)

clean:
	rm rdma
