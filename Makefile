LDFLAGS = -libverbs -lboost_program_options

# all: node master client
all: client server

node: node.cc
	$(CXX) $^ -g -o node.exe $(LDFLAGS)

master: master.cc
	$(CXX) $^ -g -o master.exe $(LDFLAGS)

client: client.cpp
	$(CXX) $^ -g -o client.exe $(LDFLAGS)

server: server.cpp
	$(CXX) $^ -g -o server.exe $(LDFLAGS)

clean:
	rm *.exe
