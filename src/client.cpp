// client.cpp
// usage: client <ip> <port>

#include <iostream>
#include <thread>
#include <atomic>
#include <ctime>
#include <chrono>

#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "packet.h"

using Clock = std::chrono::high_resolution_clock;

//
// global variabls shared between the time ticker and udp server. 
//
// The tick keeper.
std::atomic<std::uint64_t> g_ticks(0);

// error && exit.
void error(const char *msg) {
	perror(msg);
	exit(1);
}

void do_output()
{
	// timestamp in ns, for debugging purpose.
	std::cout << Clock::now().time_since_epoch().count() << ": ";

	std::cout << (g_ticks.load() % 10) + 1 << std::endl;
	g_ticks++;
}

int main(int argc, char **argv)
{
	// check command line arguments
	if (argc != 3) {
		fprintf(stderr, "usage: %s <hostname> <port>\n", argv[0]);
		exit(0);
	}
	const char *hostname = argv[1];
	int portno = atoi(argv[2]);

	// socket: create the socket
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		error("ERROR opening socket");

	// let the recvfrom timeout after 1 second
	struct timeval timeout = { 1,0 };
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(struct timeval));

	// gethostbyname: get the server's DNS entry
	hostent *server = gethostbyname(hostname);
	if (server == NULL) {
		fprintf(stderr, "ERROR, no such host as %s\n", hostname);
		exit(0);
	}

	// build the server's Internet address
	sockaddr_in serveraddr;
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)server->h_addr,
		(char *)&serveraddr.sin_addr.s_addr, server->h_length);
	serveraddr.sin_port = htons(portno);


	// periodically ping server to sync up.
	uint64_t seq_counter = 0;
	while (1) {
		// prepare the ping message
		sync_message msg;
		memset(&msg, 0, sizeof(msg));
		msg.seq = seq_counter++;
		msg.t1 = Clock::now().time_since_epoch().count();

		// send the ping to the server 
		unsigned int serverlen = sizeof(serveraddr);
		msg.hton();
		int msg_size = sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *) &serveraddr, serverlen);

		// receive the server's reply, and stamp recveiving time.
		msg_size = recvfrom(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *) &serveraddr, &serverlen);
		msg.ntoh();
		msg.t4 = Clock::now().time_since_epoch().count();

		// check for timeout
		if (-1 == msg_size) {
			// time out, we have waited for 1 sec so no need to sleep.
			std::cerr << "timeout. " << std::endl;
			do_output();
			continue;
		}

		// check for sequence number
		if (msg.seq != seq_counter) {
			std::cerr << "wrong sequence. expecting " << seq_counter
				      << ", got " << msg.seq << std::endl;
			// Ignore out of order packets.
			std::this_thread::sleep_for(std::chrono::seconds(1));
			do_output();
			continue;
		}

		// callibrate the sleep time based on server response.
		g_ticks = msg.server_ticks;
		std::this_thread::sleep_for(std::chrono::nanoseconds(msg.time_to_fire - msg.oneway_delay()));

		do_output();

		msg.dump();
	}

	return 0;
}
