LDFLAGS = -libverbs -lboost_program_options

all: main

main: main.cc
	$(CXX) $^ -o $@ $(LDFLAGS)

clean:
	rm main
