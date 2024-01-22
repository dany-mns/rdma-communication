LDFLAGS = -libverbs -lboost_program_options

all: node master 

node: node.cc
	$(CXX) $^ -g -o node.exe $(LDFLAGS)

master: master.cc
	$(CXX) $^ -g -o master.exe $(LDFLAGS)

clean:
	rm *.exe
