// server.cpp
// usage: server <port>

#include <iostream>
#include <iomanip>
#include <fstream>
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
// Global variabls shared between the ticking thread and the udp server. 
//
// The tick keeper.
std::atomic<std::uint64_t> g_ticks(0);

// The timepoint when the ticker thread started. 
// Set once, read only later on, no need to lock.
auto g_starttime = Clock::now();


// Output number 1->10 in 1 second interval
int ticking_thread()
{
	g_starttime = Clock::now();

#define DEBUG
#ifdef DEBUG
	// Write server epoch to secret file for client to benchmark.
	std::ofstream secret_file;
	secret_file.open("secret.txt");
	secret_file << g_starttime.time_since_epoch().count();
	secret_file.close();
#endif

	uint64_t last = 0;
	while (1) {
		// timestamp in ns, for debugging purpose.
		uint64_t now = Clock::now().time_since_epoch().count();
		std::cout << "[" << now << ", after " << std::setw(7) << (now - last) / 1000 << " us]: ";
		last = now;

		// output 1->10 as required.
		std::cout << (g_ticks % 10) + 1 << std::endl;
		g_ticks++;
		std::this_thread::sleep_until(g_starttime + std::chrono::seconds(g_ticks));
	}

	return 0;
}


int main(int argc, char **argv) 
{
	struct sockaddr_in serveraddr; /* server's addr */
	struct sockaddr_in clientaddr; /* client addr */

	// Check command line arguments
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	int portno = atoi(argv[1]);

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		error("ERROR opening socket");

	// setsockopt: Handy debugging trick that lets
	// us rerun the server immediately after we kill it;
	// otherwise we have to wait about 20 secs.
	// Eliminates "ERROR on binding: Address already in use" error.
	int opt = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(int));

	// Build the server's Internet address
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)portno);

	// bind: associate the parent socket with a port
	if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
		error("ERROR on binding");

	// Launch the ticker thread now.
	std::thread ticker(ticking_thread);

	// Main loop: wait for a datagram, then echo it
	unsigned int clientlen = sizeof(clientaddr); /* byte size of client's address */
	while (1) {
		// recvfrom: receive a UDP datagram from a client
		sync_message msg;
		memset(&msg, 0, sizeof(msg));

		int msg_size = recvfrom(sockfd, &msg, sizeof(msg), 0,
								(struct sockaddr *) &clientaddr, &clientlen);
		if (msg_size < 0)
			error("ERROR in recvfrom");

		msg.ntoh();

		// Set timestamp t2 -- time receiving of ping
		msg.t2 = Clock::now().time_since_epoch().count();

		// Determine who sent the datagram
		hostent *hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
									sizeof(clientaddr.sin_addr.s_addr), AF_INET);
		if (hostp == NULL)
			error("ERROR on gethostbyaddr");
		//std::cout << "server received datagram from " << hostp->h_name << ", " 
		//		  << inet_ntoa(clientaddr.sin_addr) << std::endl;

		//
		// Construct the response and send.
		//
		msg.seq++;
		msg.server_ticks = g_ticks;
		auto now = Clock::now();
		auto next_firing = g_starttime + std::chrono::seconds(msg.server_ticks);

		// If ticking thread was woke up late, it is possible next_firing is in
		// the past. In this bump up server tick in msg and recalculate.
		if (now > next_firing) {
			msg.server_ticks++;
			next_firing = g_starttime + std::chrono::seconds(msg.server_ticks);
		}

		//std::cout << "Next firing: " << next_firing.time_since_epoch().count() 
		//	      << " vs now: " << now.time_since_epoch().count() 
		//	      << ", diff="<<(next_firing - now).count() << std::endl;
		msg.time_to_fire = (next_firing - now).count();

		// Set timestamp t3 -- time sending pong. 
		msg.t3 = Clock::now().time_since_epoch().count();

		msg.hton();
		msg_size = sendto(sockfd, &msg, sizeof(msg), 0,
			(struct sockaddr *) &clientaddr, clientlen);
		if (msg_size < 0)
			error("ERROR in sendto");

		//msg.ntoh(); msg.dump();
	}

	ticker.join();
	return 0;
}
