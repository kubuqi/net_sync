server: 
	g++ -o server src/server.cpp -I./src -lpthread

client: 
	g++ -o client src/client.cpp -I./src -lpthread

all: server client

clean:
	rm server client
