#ifndef __SERVER_CPP__
#define __SERVER_CPP__

#include "server_class.hpp"

#include <fstream>

// for math string evaluation
#include "exprtk.hpp"

// for poisson distribution
#include <random>

// for multi thread
#include <pthread.h>
#include <semaphore.h>

using namespace std;

typedef void * (*thread_ptr)(void *);

string calc(string expression_str){

	//exprtk
	exprtk::parser<double> parser;
	exprtk::expression<double> expression;
	if(parser.compile(expression_str, expression))
	{
		double result = expression.value();
		return to_string(result);
	}
	else
		return "NAN";
}

server_class::server_class(){

	// creating socket file descriptor 
	if ( (welcome_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("welcome socket creation failed"); 
		exit(EXIT_FAILURE); 
	} 

	// initialize servaddr
	memset(&servaddr, 0, sizeof(servaddr));

	// setting server information
	servaddr.sin_family = AF_INET; // IPv4
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(PORT);

	// bind the socket with the server address
	if ( bind(welcome_sockfd, (const struct sockaddr *)&servaddr, 
		sizeof(servaddr)) < 0 ){
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
}

void server_class::welcome_sock_listen(){

	srand(time(NULL));

	int n, i=0, port = 8081;
	socklen_t len;

	pthread_t response_thread[100];
	struct sockaddr_in cliaddr;
	len = sizeof(cliaddr);
	struct packet mypacket;

	while(1){
		cout << "listening...\n";
		n = recvfrom(welcome_sockfd, (struct packet *)&mypacket, sizeof(mypacket),
					MSG_CONFIRM, ( struct sockaddr *) &cliaddr,
					(socklen_t *)&len);
		cout << "message from client, seq_num: " << mypacket.myheader.seq_num << endl;

		// create the connection socket arguments
		struct conn_thread_args* conn_args;
		conn_args = (struct conn_thread_args*)malloc(sizeof(struct conn_thread_args));
		conn_args->cliaddr = cliaddr;
		conn_args->recvpacket = mypacket;
		conn_args->port_num = port++;
		conn_args->seq_num = rand()%10000+1;

		if(pthread_create(&response_thread[i++], NULL, 
			(thread_ptr)&server_class::connection_sock, // pointer to member function
				(void *) conn_args                          // connection_socket argument
					) != 0){
							cout << "server pthread create failed: " << i << "th\n";
						}
	}

	i=0;
	if(i<1) pthread_join(response_thread[i++], NULL);
}

struct packet server_class::create_pkt(uint32_t seq_num, uint32_t ack_num, int rwnd, char message[BUFFERSIZE], string type, int data_length=BUFFERSIZE){

	struct packet mypacket;
	mypacket.myheader.src_port=PORT;
	mypacket.myheader.des_port=0;
	mypacket.myheader.seq_num=seq_num;
	mypacket.myheader.ack_num=ack_num;
	mypacket.myheader.len=0x50; // 01010000
	mypacket.myheader.rwnd = rwnd;
	mypacket.myheader.checksum = 0;
	// for fast recovery
	if(rwnd == -1)
		mypacket.myheader.urgent = 1;
	else
		mypacket.myheader.urgent = 0;

	if(type == "syn_ack") mypacket.myheader.flags = 0x12; // 00010010
	else if(type == "message"){
		mypacket.myheader.flags = 0x00;
		for(int p=0;p<data_length;p++){
			mypacket.message[p] = message[p];
		}
		mypacket.message_length = data_length;
	}

	return mypacket;
}

void* server_class::connection_sock(void* args){
	struct sockaddr_in cliaddr = ((struct conn_thread_args*)args)->cliaddr;
	int port = ((struct conn_thread_args*)args)->port_num;
	int ack_num = ((struct conn_thread_args*)args)->recvpacket.myheader.seq_num +1;
	int seq_num = ((struct conn_thread_args*)args)->seq_num;
	int rwnd;
	int cwnd = 1;
	int send_base;
	char video_buffer[BUFFERSIZE];
	socklen_t len = sizeof(cliaddr);

	// for step 5
	int threshold = 64;
	// 0 for slow start, 1 for congestion avoidance, 2 for fast recovery
	int congestion_state = 0;
	pair<int, int> dup_count;
	dup_count.first = -1;
	dup_count.second = 0;
	int cong_count = 0;

	// for step 6
	fd_set rfd;

	// initialize the server_wnd
	// three state for the server_wnd: 0 for not sent, 1 for sent & waiting ack
	vector<pair<struct packet, bool>>server_wnd;

	// cout << "get port number: " << ((struct conn_thread_args*)args)->port_num << endl;
	// cout << "seq_num from client: " << ((struct conn_thread_args*)args)->recvpacket.myheader.seq_num << endl;

	// information for connection_socket
	int conn_sockfd;
	struct sockaddr_in connaddr;

	if ( (conn_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("welcome socket creation failed"); 
		exit(EXIT_FAILURE); 
	}

	// initialize connaddr
	memset(&connaddr, 0, sizeof(connaddr));

	// setting connection socket information
	connaddr.sin_family = AF_INET; // IPv4
	connaddr.sin_addr.s_addr = INADDR_ANY;
	connaddr.sin_port = htons(port);

	// cout << "binding port number..." << port << endl;

	// bind the connection socket with the server address
	if ( bind(conn_sockfd, (const struct sockaddr *)&connaddr, 
		sizeof(connaddr)) < 0 ){
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	// threeway handshake: start from SYN ACK
	struct packet mypacket = create_pkt(seq_num, ack_num, rwnd, video_buffer, "syn_ack");
	struct packet recvpacket;

	sendto(conn_sockfd, (const struct packet *) &mypacket, sizeof(mypacket),
			MSG_CONFIRM, (const struct sockaddr *) &cliaddr, len);
	recvfrom(conn_sockfd, (struct packet*) &recvpacket, sizeof(recvpacket),
			MSG_CONFIRM, (struct sockaddr *) &cliaddr, (socklen_t* ) &len);
	seq_num = recvpacket.myheader.ack_num;
	ack_num = recvpacket.myheader.seq_num + 1;

	cout << "=====Complete the three-way handshake=====\n";
	// threeway handshake complete

	do{
		if(recvpacket.message_length != 65535)
		{
			cout<<"\nNot request from client"<< endl;
		}
		else
		{
			server_wnd.resize(0);
			vector<packet> response_queue;
			seq_num = recvpacket.myheader.ack_num;
			ack_num = recvpacket.myheader.seq_num + 1;
			send_base = seq_num;

			// get request from the recvpacket
			string request = recvpacket.message;
			cout << "\n\nget request from client: " << request << endl;
			cout << "try video operation first";

			// start reading the video file...
			ifstream fp(request, ios::binary | ios::ate);

			if(!fp){
				// cannot open the file
				cout << "cannot open the file" << endl;
				cout << "try calculation operation\n";
				string ans = calc(request);
				int len_ans = ans.length();
				cout << "answer: " << ans << endl;
				char calc_buffer[len_ans+2];
				for(int p=0;p<len_ans;p++) calc_buffer[p] = ans.c_str()[p];
				calc_buffer[len_ans] = '\n';
				calc_buffer[len_ans+1] = 0;
				response_queue.push_back(create_pkt(seq_num, ack_num, rwnd, calc_buffer, "message", len_ans+2));
				seq_num+=1;
			}
			else
			{
				int video_size = fp.tellg();
				cout << "video_size: " << video_size << endl;
				fp.seekg(ios::beg);

				int count = 0;

				while(!fp.eof()){
					size_t rb = fp.read(video_buffer, BUFFERSIZE).gcount();
					if(count == 3)
						response_queue.push_back(create_pkt(seq_num, ack_num, -1, video_buffer, "message", rb));
					else
						response_queue.push_back(create_pkt(seq_num, ack_num, rwnd, video_buffer, "message", rb));
					seq_num+=1;
					count++;
				}
			}

			// cout << "all seq_num in the respose_queue: \n";
			// for(int p = 0;p < response_queue.size();p++){
			//     cout << response_queue[p].myheader.seq_num << ", ";
			// }
			// cout << endl;

			cout << "response_queue create complete\n";

			// put packet into the window
			int last_cwnd_pkt = 0;
			for(;last_cwnd_pkt < cwnd;last_cwnd_pkt++){
				server_wnd.push_back(make_pair(response_queue[last_cwnd_pkt], 0));
			}
			// cout << "length of response_queue: " << response_queue.size() << endl;
			// cout << "first seq_num: " << response_queue[0].myheader.seq_num << endl;
			// cout << "server_wnd" << server_wnd[0].first.myheader.seq_num << endl;
			// cout << "last seq_num: " << response_queue[response_queue.size()-1].myheader.seq_num << endl;
			// for(int p = 0;p < cwnd;p++){
			//     cout << server_wnd[p].first.myheader.seq_num << ", ";
			// }
			cout << endl;
			// waiting for the ack of cwnd_base
			struct timeval timeout_block;
			timeout_block.tv_sec = 0;
			timeout_block.tv_usec = 0;
			setsockopt(conn_sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout_block, sizeof(timeout_block));

			cout << "*****Slow Start*****\n";
			while(1){
				// send the packet in the cwnd
				for(int s=0;s<cwnd;s++){
					if(!server_wnd[s].second && server_wnd[s].first.myheader.seq_num != 0){
						sendto(conn_sockfd, (const struct packet *) &(server_wnd[s].first), sizeof(server_wnd[s].first),
								MSG_CONFIRM, (const struct sockaddr *) &cliaddr,
								len);
						server_wnd[s].second = true;
						// cout << "\tSend a packet ( seq_num = " << server_wnd[s].first.myheader.seq_num << " )\n";
					}
				}
				
				// receive the packet by blocking way
				// int n = recvfrom(conn_sockfd, (struct packet*) &recvpacket, sizeof(recvpacket),
				//                     MSG_CONFIRM, (struct sockaddr *) &cliaddr,
				//                     (socklen_t* ) &len);

				// receive the packet by non-blocking way
				FD_ZERO(&rfd);
				FD_SET(conn_sockfd, &rfd);

				struct timeval timeout;
				timeout.tv_sec = 1;
				timeout.tv_usec = 1000000;
				int n = select(conn_sockfd + 1, &rfd, NULL, NULL, &timeout);
				if(n == -1){
					cout << "error\n";
					break;
				}
				else if(n == 0){
					// timeout
					server_wnd[0].second = false;
					cout << "timeout occurred for send_base: " << send_base << endl;
					// int test;
					// cin >> test;
					if(send_base >= response_queue[response_queue.size()-1].myheader.seq_num-1) continue;
					else if(cwnd == 1) continue;
					else if(congestion_state == 0){
						congestion_state = 0;
						cout << "*****Enter Slow Start By Timeout*****\n";
						last_cwnd_pkt-=(cwnd);
						threshold = cwnd / 2;
						if(threshold < 2) threshold = 2;
						cwnd = 1;
						dup_count.first = -1;
						dup_count.second = 0;
						server_wnd.resize(cwnd);
						server_wnd[0] = make_pair(response_queue[last_cwnd_pkt++], false);
						cout << "cwnd: " << cwnd << ", threshold: " << threshold << endl;
						// cout << "ack_num: " << send_base << endl;
						// cout << "server_wnd.seq_num: " << server_wnd[0].first.myheader.seq_num << endl;
						// int test;
						// cin >> test;
					}
					else if(congestion_state == 1){
						// cong avoidance --> slow start
						congestion_state = 0;
						cout << "*****Congestion Control--> Slow Start*****\n";
						last_cwnd_pkt-=(cwnd);
						threshold = cwnd / 2;
						if(threshold < 2) threshold = 2;
						cwnd = 1;
						dup_count.first = -1;
						dup_count.second = 0;
						server_wnd.resize(cwnd);
						server_wnd[0] = make_pair(response_queue[last_cwnd_pkt++], false);
						cout << "cwnd: " << cwnd << ", threshold: " << threshold << endl;
						// cout << "ack_num: " << send_base << endl;
						// cout << "server_wnd.seq_num: " << server_wnd[0].first.myheader.seq_num << endl;
						// int test;
						// cin >> test;
					}
					else if(congestion_state == 2){
						congestion_state = 0;
						cout << "*****Fast Recovery--> Slow Start*****\n";
						last_cwnd_pkt-=(cwnd);
						threshold = cwnd / 2;
						if(threshold < 2) threshold = 2;
						cwnd = 1;
						dup_count.first = -1;
						dup_count.second = 0;
						server_wnd.resize(cwnd);
						server_wnd[0] = make_pair(response_queue[last_cwnd_pkt++], false);
						cout << "cwnd: " << cwnd << ", threshold: " << threshold << endl;
						// cout << "ack_num: " << send_base << endl;
						// cout << "server_wnd.seq_num: " << server_wnd[0].first.myheader.seq_num << endl;
					}
					continue;
				}
				else{
					// have packet to receive
					// assign something wrong
					if(!FD_ISSET(conn_sockfd, &rfd)){
						cout << "assigning something wrong\n";
						break;
					}
					int block_n = recvfrom(conn_sockfd, (struct packet*) &recvpacket, sizeof(recvpacket),
											MSG_CONFIRM, (struct sockaddr *) &cliaddr,
											(socklen_t* ) &len);
					int new_send_base = (int)(recvpacket.myheader.ack_num);
					cout << "\tReceive a packet ( ack_num = " << recvpacket.myheader.ack_num << " )\n";
					// cout << "get new ack_num: " << new_send_base << endl;
					// cout << "send_base: " << send_base << endl;

					// 3-duplicate ack
					if(congestion_state == 2 && new_send_base == dup_count.first){
						dup_count.second+=1;
						if(dup_count.second >= 10){
							congestion_state = 1;
							threshold = cwnd / 2;
							if(threshold < 2) threshold = 2;
							last_cwnd_pkt-=cwnd;
							cwnd = 1;
							server_wnd.resize(cwnd);
							for(int a =0;a<server_wnd.size();a++){
								if(last_cwnd_pkt < response_queue.size())
									server_wnd[a] = make_pair(response_queue[last_cwnd_pkt++], false);
								else{
									struct packet tmppacket;
									tmppacket.myheader.seq_num = 0;
									server_wnd[a] = make_pair(tmppacket, false);
									last_cwnd_pkt++;
								}
							}
							cout << "*****Fast Recovery --> Slow Start*****\n";
							cout << "cwnd: " << cwnd << ", threshold: " << threshold << endl;
							continue;
						}
						cout << "*****Retransmit in Fast Recovery****\n";
						cwnd++;
						sendto(conn_sockfd, (const struct packet *) &(server_wnd[0].first), sizeof(server_wnd[0].first),
								MSG_CONFIRM, (const struct sockaddr *) &cliaddr,
								len);
						cout << "Send a packet ( seq_num: " << server_wnd[0].first.myheader.seq_num << " ) \n";
						server_wnd.push_back(make_pair(response_queue[last_cwnd_pkt++], false));
						// for(int f=0;f<server_wnd.size();f++) server_wnd[f].second = false;
						// cout << "cwnd: " << cwnd << ", threshold: " << threshold << endl;
						// cout << "ack_num: " << recvpacket.myheader.ack_num << endl;
						// cout << "send_base: " << send_base << endl;
						// cout << "server_wnd.seq_num: " << server_wnd[0].first.myheader.seq_num << endl;
						// int test;
						// cin >> test;
						continue;
					}

					if(new_send_base != dup_count.first){
						dup_count.first = new_send_base;
						dup_count.second = 0;
					}
					else dup_count.second+=1;

					if(dup_count.second == 3){
						cout << "*****Fast Transmit & Enter Fast Recovery*****\n";
						sendto(conn_sockfd, (const struct packet *) &(server_wnd[0].first), sizeof(server_wnd[0].first),
								MSG_CONFIRM, (const struct sockaddr *) &cliaddr,
								len);
						congestion_state = 2;
						threshold = cwnd / 2;
						if(threshold < 2) threshold = 2;
						last_cwnd_pkt-=cwnd;
						cwnd = threshold+3;
						server_wnd.resize(cwnd);
						for(int a =0;a<server_wnd.size();a++){
							if(last_cwnd_pkt < response_queue.size())
								server_wnd[a] = make_pair(response_queue[last_cwnd_pkt++], false);
							else{
								struct packet tmppacket;
								tmppacket.myheader.seq_num = 0;
								server_wnd[a] = make_pair(tmppacket, false);
								last_cwnd_pkt++;
							}
						}
						server_wnd[0].second = false;
						cout << "cwnd: " << cwnd << ", threshold: " << threshold << endl;
						// cout << "ack_num: " << recvpacket.myheader.ack_num << endl;
						// cout << "send_base: " << send_base << endl;
						// cout << "server_wnd.seq_num: " << server_wnd[0].first.myheader.seq_num << endl;
						// int test;
						// cin >> test;
						continue;
					}
					if(new_send_base > response_queue[response_queue.size()-1].myheader.seq_num){
						cout << "transmition complete\n";
						break;
					}
					else if(((send_base < new_send_base) && (send_base + cwnd >= new_send_base)) || (send_base < new_send_base)){
						// cout << "update the server_wnd...\n";
						// update server_wnd...
						// receive packet's ack is within the window
						if((send_base < new_send_base) && (send_base + cwnd >= new_send_base)){
							int num_to_add = new_send_base - send_base;
							move(server_wnd.begin() + (num_to_add), server_wnd.end(), server_wnd.begin());
							for(int a = num_to_add; a>0; a--){
								if(last_cwnd_pkt < response_queue.size())
									server_wnd[cwnd - a] = make_pair(response_queue[last_cwnd_pkt++], false);
								else{
									struct packet tmppacket;
									tmppacket.myheader.seq_num = 0;
									server_wnd[cwnd - a] = make_pair(tmppacket, false);
									last_cwnd_pkt++;
								}
							}
						}
						// receive packet's ack is out (larger than) of the window
						else if(send_base + cwnd < new_send_base){
							last_cwnd_pkt+=(new_send_base - send_base - cwnd);
							for(int a =0;a<server_wnd.size();a++){
								if(last_cwnd_pkt < response_queue.size())
									server_wnd[a] = make_pair(response_queue[last_cwnd_pkt++], false);
								else{
									struct packet tmppacket;
									tmppacket.myheader.seq_num = 0;
									server_wnd[a] = make_pair(tmppacket, false);
									last_cwnd_pkt++;
								}
							}
						}
						send_base = new_send_base;

						// for slow start
						if(congestion_state == 0){
							struct packet tmppacket;
							tmppacket.myheader.seq_num = 0;
							if(last_cwnd_pkt < response_queue.size())
								server_wnd.push_back(make_pair(response_queue[last_cwnd_pkt++], false));
							else{
								server_wnd.push_back(make_pair(tmppacket, false));
								last_cwnd_pkt++;
							}
							if(++cwnd >= threshold){
								cout << "*****Congestion Avoidance*****\n";
								congestion_state = 1;
								// update the dup count
								dup_count.first = -1;
								dup_count.second = 0;
							}
							cout << "cwnd: " << cwnd << ", threshold: " << threshold << endl;
							// cout << "ack_num: " << recvpacket.myheader.ack_num << endl;
							// cout << "send_base: " << send_base << endl;
						}
						// for cong_control
						else if(congestion_state == 1){
							if(++cong_count == cwnd){
								cong_count = 0;
								cwnd++;
								struct packet tmppacket;
								tmppacket.myheader.seq_num = 0;
								if(last_cwnd_pkt < response_queue.size())
									server_wnd.push_back(make_pair(response_queue[last_cwnd_pkt++], false));
								else{
									server_wnd.push_back(make_pair(tmppacket, false));
									last_cwnd_pkt++;
								}
								cout << "cwnd: " << cwnd << ", threshold: " << threshold << endl;
								// cout << "ack_num: " << recvpacket.myheader.ack_num << endl;
								// cout << "send_base: " << send_base << endl;
							}
							// if(cong_count == 30){
							//     int test;
							//     cin >> test;
							// }
						}
						else if(congestion_state == 2){
							cout << "*****Fast Recovery--> Congestion Avoidance*****\n";
							congestion_state = 1;
							last_cwnd_pkt-=cwnd;
							cwnd = threshold;
							dup_count.second = 0;
							server_wnd.resize(cwnd);
							for(int a =0;a<server_wnd.size();a++){
								if(last_cwnd_pkt < response_queue.size())
									server_wnd[a] = make_pair(response_queue[last_cwnd_pkt++], false);
								else{
									struct packet tmppacket;
									tmppacket.myheader.seq_num = 0;
									server_wnd[a] = make_pair(tmppacket, false);
									last_cwnd_pkt++;
								}
							}
							continue;
						}
						
						// cout << "after updating the last_cwnd_pkt, last_cwnd_num: " << last_cwnd_pkt << ",  new_send_base: " << new_send_base << endl;

						// for(int p = 0;p < 10 && p < server_wnd.size();p++){
						//     cout << server_wnd[p].first.myheader.seq_num << ", ";
						// }
						// cout << endl;

						// if(cwnd == 51){
						//     int test;
						//     cin >> test;
						// }
					}
					else{
						// server_wnd[0].second = false;
						continue;
					}
				}
			}

			strcpy(video_buffer, "end");
			struct packet endpacket = create_pkt(seq_num, ack_num, rwnd, video_buffer, "message", 4);
			sendto(conn_sockfd, (const struct packet *) &(endpacket), sizeof(endpacket),
						MSG_CONFIRM, (const struct sockaddr *) &cliaddr,
						len);
			seq_num+=1;
		}
		recvfrom(conn_sockfd, (struct packet*) &recvpacket, sizeof(recvpacket),
					MSG_CONFIRM, (struct sockaddr *) &cliaddr,
					(socklen_t* ) &len);

	}while(recvpacket.myheader.flags != 0x01);
	
	// close socket
	cout << "Connection socket closing... " << pthread_self() << endl;
	close(conn_sockfd);

	return NULL;
}

# endif
