LDFLAGS = -libverbs -lboost_program_options

all: server client 

server: server.cc
	$(CXX) $^ -g -o server.exe $(LDFLAGS)

client: client.cc
	$(CXX) $^ -g -o client.exe $(LDFLAGS)

clean:
	rm *.exe
