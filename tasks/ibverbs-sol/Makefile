LDFLAGS = -libverbs -lboost_program_options

all: rdma bruck pairwise
	./upload.sh

rdma: rdma.cc
	$(CXX) $^ -o $@ $(LDFLAGS)

bruck: bruck.cc
	$(CXX) $^ -o $@ $(LDFLAGS)

pairwise: pairwise.cc
	$(CXX) $^ -o $@ $(LDFLAGS)

clean:
	rm rdma bruck pairwise
