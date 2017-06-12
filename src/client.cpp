// client.cpp
// usage: client <ip> <port>

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
using Seconds = std::chrono::seconds;
using MilliSeconds = std::chrono::milliseconds;
using MicroSeconds = std::chrono::microseconds;
using NanoSeconds = std::chrono::nanoseconds;

// Time in seconds between two synchronization requests.
#define SYNC_INTERVAL 2

//
// Global variabls
//

// Calculated starting timestamp of server tick thread, on local clock.
// This timestamp will be updated by messaging thread to try sync with server's
// start time, and the output thread will pace accordingly. 
std::atomic<std::uint64_t> g_server_start_time(0);


// Output number 1->10 in 1 second interval
int ticking_thread()
{
	// Initialize with local time for now, and it will be calibrated later on.
	g_server_start_time = Clock::now().time_since_epoch().count();

	uint64_t last = 0;
	while (1) {
		auto server_epoch = g_server_start_time.load();
		uint64_t now = Clock::now().time_since_epoch().count();

		std::cout << "[" << now << ", after " << std::setw(7) << (now - last) / 1000 << " us]: ";
		last = now;

		// Output 1->10 as required. Convert from nano-seconds to seconds.
		// Add 500ms so as to avoid falling short by a few leads to lossing 1 
		// whole second because of integer division.
		auto ticks = (now - server_epoch + 500*1000*1000) / (1000 * 1000 * 1000);
		std::cout << (ticks % 10) + 1 << std::endl;

		// Sleep till next tick.
		auto server_time_point = Clock::time_point(NanoSeconds(server_epoch));
		std::this_thread::sleep_until(server_time_point + Seconds(ticks+1));
	}

	return 0;
}

//
// Calibrate the start time.
//
void calibrate(uint64_t ttf, uint64_t server_tick)
{
	// Currently just update it with new value. A better way is to use a filter 
	// like kalman filter to stablize the result.
	uint64_t new_epoch = ttf - server_tick * 1000 * 1000 * 1000;
	g_server_start_time = new_epoch;


#define DEBUG
#ifdef DEBUG
	uint64_t old_epoch = g_server_start_time;
	//std::cout << "Old epoch: " << old_epoch << ", New epoch: " << new_epoch << ", Diff: "
	//	<< (old_epoch > new_epoch ? "-" : "")
	//	<< (old_epoch > new_epoch ? (old_epoch - new_epoch) : (new_epoch - old_epoch))
	//	<< " ns" << std::endl;

	// Read server epoch from secret file to do benchmarking.
	std::ifstream secret_file;
	secret_file.open("secret.txt");
	uint64_t server_epoch;
	secret_file >> server_epoch;
	secret_file.close();

	std::cout << "Checking with server epoch, diff = "
		<< (server_epoch > new_epoch ? "-" : "")
		<< (server_epoch > new_epoch ? (server_epoch - new_epoch) : (new_epoch - server_epoch))
		<< " ns" << std::endl;
#endif
}


int main(int argc, char **argv)
{
	// Check command line arguments
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

	// Let the recvfrom timeout after SYNC_INTERVAL seconds
	struct timeval timeout = { SYNC_INTERVAL,0 };
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(struct timeval));

	// gethostbyname: get the server's DNS entry
	hostent *server = gethostbyname(hostname);
	if (server == NULL) {
		fprintf(stderr, "ERROR, no such host as %s\n", hostname);
		exit(0);
	}

	// Build the server's Internet address
	sockaddr_in serveraddr;
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)server->h_addr,
		(char *)&serveraddr.sin_addr.s_addr, server->h_length);
	serveraddr.sin_port = htons(portno);

	// Launch the ticker thread now.
	std::thread ticker(ticking_thread);

	// Periodically ping server to sync up.
	uint64_t seq_counter = 0;
	while (1) {
		// Prepare the ping message
		sync_message msg;
		memset(&msg, 0, sizeof(msg));
		msg.seq = seq_counter++;
		msg.t1 = Clock::now().time_since_epoch().count();

		// Send the ping to the server 
		unsigned int serverlen = sizeof(serveraddr);
		msg.hton();
		int msg_size = sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *) &serveraddr, serverlen);

		// Receive the server's reply, and stamp recveiving time.
		msg_size = recvfrom(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *) &serveraddr, &serverlen);
		msg.ntoh();
		msg.t4 = Clock::now().time_since_epoch().count();

		// Check for timeout
		if (-1 == msg_size) {
			// time out, we have waited for 1 sec so no need to sleep.
			std::cerr << "timeout. " << std::endl;
			continue;
		}

		// Check for sequence number
		if (msg.seq != seq_counter) {
			std::cerr << "wrong sequence. expecting " << seq_counter
				      << ", got " << msg.seq << std::endl;
			// Ignore out of order packets.
			std::this_thread::sleep_for(std::chrono::seconds(SYNC_INTERVAL));
			continue;
		}

//		msg.dump();
 
		// Time to fire on local clock
		auto ttf = msg.t4 + msg.time_to_fire - msg.oneway_delay();

		// Calibrate the epoch time and server ticks, so that the 
		// ticking thread can pace itself.
		calibrate(ttf, msg.server_ticks);

		// Sleep for SYNC_INTERVAL seconds
		std::this_thread::sleep_for(std::chrono::seconds(SYNC_INTERVAL));
	}

	return 0;
}
