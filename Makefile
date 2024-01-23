LDFLAGS = -libverbs -lboost_program_options

# all: node master socket-client
all: socket-client

node: node.cc
	$(CXX) $^ -g -o node.exe $(LDFLAGS)

master: master.cc
	$(CXX) $^ -g -o master.exe $(LDFLAGS)

socket-client: socket-client.cpp
	$(CXX) $^ -g -o socket-client.exe $(LDFLAGS)

clean:
	rm *.exe
