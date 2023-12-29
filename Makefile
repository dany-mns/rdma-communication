LDFLAGS = -libverbs -lboost_program_options

all:server 

server: server.cc
	$(CXX) $^ -o server.exe $(LDFLAGS)

clean:
	rm server 
