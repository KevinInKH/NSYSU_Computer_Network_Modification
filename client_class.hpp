#ifndef __CLIENT_HPP__
#define __CLIENT_HPP__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <ctime>

#include <iostream>
#include <vector>

// network related
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

using namespace std;

#define BUFFERSIZE 	1024

// experiment parameter
// #define THRESHOLD	0.0001
// #define THRESHOLD	0.00001
// #define THRESHOLD	0.000001
#define THRESHOLD	0
// #define ISDELAYACKED true
#define ISDELAYACKED false

struct header{
	uint16_t src_port;
	uint16_t des_port;
	uint32_t seq_num;
	uint32_t ack_num;
	uint8_t len;
	uint8_t flags;
	uint16_t rwnd;
	uint16_t checksum;
	uint16_t urgent; 
};

struct packet{
	struct header myheader;
	unsigned short message_length;
	char message[BUFFERSIZE];
};

class client_class{
	private:
		int sockfd, seq_num, ack_num, rwnd, port_num, rcv_base;
		// int fast, flag; 
		int loss_time, total_time;
		string hostname;
		struct sockaddr_in servaddr;
		string ip_address;
		vector<string> message_vector;
		vector<struct packet> message_queue;
		vector<pair<int, bool>> client_wnd;
		struct timeval timeout;
		vector<struct packet> packet_buffer;
	public:
		client_class(char*, int);
		void send_pkt(struct packet);
		struct packet recv_pkt();
		void set_pkt();
		struct packet create_pkt(string, string, unsigned short = 0);
		void* processing(void*);
		void est_connection();
		void set_request(string);
		void write_file(ofstream &fp);
};

#endif
