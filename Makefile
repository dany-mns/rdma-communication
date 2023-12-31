LDFLAGS = -libverbs -lboost_program_options

all:server 

server: server.cc
	$(CXX) $^ -g -o server.exe $(LDFLAGS)

clean:
	rm server.exe
