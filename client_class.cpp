#ifndef __CLIENT_CPP__
#define __CLIENT_CPP__

#include "client_class.hpp"

#include <fstream>
#include <algorithm>

// for multi thread
#include <pthread.h>
#include <semaphore.h>

using namespace std;

client_class::client_class(char* hostname, int port_num){

	// cout << hostname << " "  << port_num << endl;
	this->hostname = hostname;
	this->port_num = port_num;
	// flag = 0;
	loss_time = 0;
	total_time = 0;

	srand(time(NULL));
	seq_num = rand()%10000+1;
	rwnd = 10240;
	packet_buffer.reserve(524288/(BUFFERSIZE+20+2));
	struct packet tmppacket;
	tmppacket.myheader.seq_num=-1;
	for(int i=0;i<(524288/(BUFFERSIZE+20+2));i++) packet_buffer.push_back(tmppacket);
	client_wnd.reserve((524288/(BUFFERSIZE+20+2)));
	for(int i=0;i<(524288/(BUFFERSIZE+20+2));i++) client_wnd.push_back(make_pair(-1,false));

	// cout << "in the constructor...\n";
	// for(int p=0;p<2;p++){
	// 	cout << "( " << client_wnd[p].first << ", " << client_wnd[p].second << " ), ";
	// }
	// cout << endl;

	// creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	// initialize servaddr
	memset(&servaddr, 0, sizeof(servaddr));

	// setting server information
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port_num);
	servaddr.sin_addr.s_addr = inet_addr(hostname);
}

void client_class::est_connection(){
	// closed :0 --> send syn --> <wait> --> receive syn_ack :1 --> ack_sent --> established --> return est_success 
	
	struct packet syn_packet;
	syn_packet=create_pkt("syn", "");
	send_pkt(syn_packet);
	// wait for syn_ack
	struct packet recvpacket = recv_pkt();
	ack_num = recvpacket.myheader.seq_num + 1;
	seq_num = recvpacket.myheader.ack_num;

	set_pkt();
	// sent and established succeeded
	send_pkt(message_queue[0]);
	cout << "sending request with seq_num: " << message_queue[0].myheader.seq_num << ", message: " << message_queue[0].message << endl;

	return;
}

void client_class::set_request(string request){
	message_vector.push_back(request);
	return;
}

void client_class::set_pkt(){
	for(int i=0;i<message_vector.size();i++){
		cout << "message_vector: " << message_vector[i] << endl;
		if(!i) message_queue.push_back(create_pkt("ack", message_vector[i], 65535));
		else message_queue.push_back(create_pkt("message", message_vector[i], 65535));
		seq_num+=1;
	}
	cout<<"size of message queue : "<<message_queue.size()<<endl;
	return;
}

struct packet client_class::create_pkt(string type, string message,unsigned short message_length){
	struct packet mypacket;
	mypacket.myheader.src_port=0;
	mypacket.myheader.des_port=port_num;
	if(message == "error")
		mypacket.myheader.seq_num = 0;
	else
		mypacket.myheader.seq_num=seq_num;
	mypacket.myheader.ack_num=ack_num;
	mypacket.myheader.len=0x50; // 01010000
	mypacket.myheader.rwnd = rwnd;
	mypacket.myheader.checksum = 0;
	mypacket.myheader.urgent = 0;

	if(type == "syn") mypacket.myheader.flags = 0x02; 		// 00000010
	else if(type == "ack") mypacket.myheader.flags = 0x10; 	// 00010000
	else if(type == "fin") mypacket.myheader.flags = 0x01; 	// 00000001
	else mypacket.myheader.flags = 0x00;					// 00000000

	mypacket.message_length = message_length;

	strcpy(mypacket.message, message.c_str());
	return mypacket;
}

void client_class::write_file(ofstream& fp){
	int sending_final = 0;
	while(client_wnd[sending_final+1].second) sending_final++;
	for(int s=0;s<=sending_final;s++){
		char video_buffer[packet_buffer[s].message_length];
		for(int v=0;v<packet_buffer[s].message_length;v++) video_buffer[v] = packet_buffer[s].message[v];
		fp.write(video_buffer, packet_buffer[s].message_length);
	}
	ack_num = client_wnd[sending_final].first + 1;

	// update (shift) client_wnd, packet_buffer, rcv_base
	
	// cout << "before update...\n";
	// for(int p=0;p<5;p++){
	// 	cout << "( " << client_wnd[p].first << ", " << client_wnd[p].second << " ), ";
	// }
	// cout << endl;


	rcv_base = (client_wnd[sending_final].first) + 1;
	// cout << "rcv_base: " << rcv_base << ", sending_final: " << sending_final << endl;

	move(client_wnd.begin() + (sending_final + 1), client_wnd.end(), client_wnd.begin());
	fill(client_wnd.begin() + (client_wnd.size() - sending_final - 1), client_wnd.end(), make_pair(-1, false));

	move(packet_buffer.begin() + (sending_final + 1), packet_buffer.end(), packet_buffer.begin());
	struct packet tmppacket;
	tmppacket.myheader.seq_num = -1;
	fill(packet_buffer.begin() + (packet_buffer.size() - sending_final - 1), packet_buffer.end(), tmppacket);

	// cout << "after update...\n";
	// for(int p=0;p<5;p++){
	// 	cout << "( " << client_wnd[p].first << ", " << client_wnd[p].second << " ), ";
	// }
	// cout << endl;

	return;

}

void* client_class::processing(void *arg){
	// threeway handshake
	cout << "====Start the three-way handshake=====\n";
	est_connection();
	cout << "====Complete the three-way handshake=====\n\n\n";

	string filename_prefix = "request_" + to_string(pthread_self()) + "_" + to_string((rand()));

	rcv_base = ack_num;
	bool gap =false;

	timeout.tv_sec = 0;
	timeout.tv_usec = 500000;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	// send request in message queue
	for(int i=0;i<message_queue.size();i++){
		if(i>0){
			send_pkt(create_pkt("message", message_vector[i], 65535));
		}
		// cout << "waiting for the response: " << i << endl;

		// fast = ack_num;
		// cout << "fast: " << fast << endl;

		// writing video to the mp4 file
		int video_size = 0, ptr_num = 0;
		struct packet recvpacket;

		string filename = filename_prefix + "_" + to_string((i+1));
		ofstream fp(filename, ios::binary);

		// cout << "ack_num before receiving packet: " << ack_num << endl;
		recvpacket = recv_pkt();
		while(strcmp(recvpacket.message, "end") != 0){

			// rceive from server nicely
			if(strcmp(recvpacket.message, "error") != 0){
				// in-order packet
				if(recvpacket.myheader.seq_num == ack_num){
					// client_wnd is empty
					if(!client_wnd[0].second && !gap){
						packet_buffer[recvpacket.myheader.seq_num - ack_num] = recvpacket;
						client_wnd[recvpacket.myheader.seq_num - ack_num] = make_pair(recvpacket.myheader.seq_num, true);

						if(ISDELAYACKED)
						{
							cout << "extra 500ms for the delayed ack\n";

							timeout.tv_sec = 0;
							timeout.tv_usec = 500000;
							setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
							recvpacket = recv_pkt();

							if(strcmp(recvpacket.message, "error") != 0)
							{
								// expected next packet, send delayed ack
								if(recvpacket.myheader.seq_num == ack_num + 1){

									if(!client_wnd[1].second && !gap){
										cout << "get expected packet, send delayed ack\n";
										packet_buffer[recvpacket.myheader.seq_num - ack_num] = recvpacket;
										client_wnd[recvpacket.myheader.seq_num - ack_num] = make_pair(recvpacket.myheader.seq_num, true);
									}
									// gap exist and the expected packet arrive
									else if(!client_wnd[1].second && gap){
										cout << "fill the gap: " << recvpacket.myheader.seq_num << endl;
										packet_buffer[recvpacket.myheader.seq_num - ack_num] = recvpacket;
										client_wnd[recvpacket.myheader.seq_num - ack_num] = make_pair(recvpacket.myheader.seq_num, true);
										write_file(fp);
										gap = false;
										for(int c=0;c<client_wnd.size();c++){
											if (client_wnd[c].second) {
												gap = true;
												break;
											}
										}
									}
								}
								// out-of-order packet (bigger than expected)
								else if(recvpacket.myheader.seq_num > ack_num + 1){
									cout << "out-of-order packet arrived: " << recvpacket.myheader.seq_num << endl;
									// gap exist
									gap = true;
									// put into buffer if not exceed client_wnd's size
									if((recvpacket.myheader.seq_num - ack_num) < client_wnd.size()){
										packet_buffer[recvpacket.myheader.seq_num - ack_num] = recvpacket;
										client_wnd[recvpacket.myheader.seq_num - ack_num] = make_pair(recvpacket.myheader.seq_num, true);
									}
									// send ack immedaiately
								}
								// out-of-order packet (smaller than expected)
								else{
									cout << "out-of-order packet arrived: " << recvpacket.myheader.seq_num << endl;

									// send ack immedaiately
								}
							}
							else
							{
								cout<< "no packet in extra 500ms, send ack\n";
							}
						}

						write_file(fp);
						send_pkt(create_pkt("ack", ""));
					}
					// gap exist and the expected packet arrive
					else if(!client_wnd[0].second && gap){
						cout << "fill the gap: " << recvpacket.myheader.seq_num << endl;
						packet_buffer[recvpacket.myheader.seq_num - ack_num] = recvpacket;
						client_wnd[recvpacket.myheader.seq_num - ack_num] = make_pair(recvpacket.myheader.seq_num, true);
						write_file(fp);
						gap = false;
						for(int c=0;c<client_wnd.size();c++){
							if (client_wnd[c].second) {
								gap = true;
								break;
							}
						}
						send_pkt(create_pkt("ack", ""));
					}

					// cout << "client_wnd: ";
					// for(int p=0;p<2;p++){
					// 	cout << "( " << client_wnd[p].first << ", " << client_wnd[p].second << " ), ";
					// }
					// cout << endl;
				}
				// out-of-order packet (bigger than expected)
				else if(recvpacket.myheader.seq_num > ack_num){
					cout << "out-of-order packet arrived: " << recvpacket.myheader.seq_num << endl;
					// gap exist
					gap = true;
					// put into buffer if not exceed client_wnd's size
					if((recvpacket.myheader.seq_num - ack_num) < client_wnd.size()){
						packet_buffer[recvpacket.myheader.seq_num - ack_num] = recvpacket;
						client_wnd[recvpacket.myheader.seq_num - ack_num] = make_pair(recvpacket.myheader.seq_num, true);
					}
					// send ack immedaiately
					send_pkt(create_pkt("ack", ""));
				}
				// out-of-order packet (smaller than expected)
				else{
					cout << "out-of-order packet arrived: " << recvpacket.myheader.seq_num << endl;

					// send ack immedaiately
					send_pkt(create_pkt("ack", ""));
				}

				// cout << "client_wnd: ";
				// for(int p=0;p<5;p++){
				// 	cout << "( " << client_wnd[p].first << ", " << client_wnd[p].second << " ), ";
				// }
				// cout << endl;
			}
			else {
				if(client_wnd[0].second == true){
					write_file(fp);
					send_pkt(create_pkt("ack", ""));
				}
			}

			recvpacket = recv_pkt();
		}

		cout << "loss_time: " << loss_time << endl;
		cout << "total_time: " << total_time << endl;
		cout << "loss_ratio: " << double(loss_time)/double(total_time) << endl;

		fp.close();
	}

	send_pkt(create_pkt("fin", ""));

	// close socket
	cout << "\nclient socket closing... " << pthread_self() << endl;
	close(sockfd);

	return NULL;
}

struct packet client_class::recv_pkt(){
	total_time+=1;

	struct packet mypacket;
	socklen_t len = sizeof(servaddr);
	int n = recvfrom(sockfd, (struct header *)&mypacket, sizeof(mypacket), MSG_CONFIRM, (struct sockaddr *) &servaddr, (socklen_t* )&len);
	if (n < 0){
		cout << "fail to recv from server\n";
		return create_pkt("message", "error");
	}
	else{
		// if(mypacket.myheader.seq_num - fast == 3 && flag == 0){
		// 	if(!flag) flag = 1;
		// 	cout << "send three acks to enter fast recovery, ack_num: " << ack_num << endl;
		// 	struct packet fast_rec_pkt = create_pkt("ack", "");
		// 	send_pkt(fast_rec_pkt);
		// 	send_pkt(fast_rec_pkt);
		// 	send_pkt(fast_rec_pkt);
		// 	return create_pkt("message", "fast");
		// }
		if(((double) rand() / (RAND_MAX)) < THRESHOLD){
			loss_time+=1;
			cout << "*****loss packet*****\n";
			return create_pkt("message", "error");
		}
	}
	if(mypacket.myheader.flags == 0x12) cout << "Receive a packet (SYN_ACK) from " << ip_address << mypacket.myheader.src_port << endl;
	cout << "\tRecieve a packet (seq_num = " << mypacket.myheader.seq_num << ", ack_num = " << mypacket.myheader.ack_num << ")\n";
	// seq_num = mypacket.myheader.ack_num;
	return mypacket;
}

void client_class::send_pkt(struct packet mypacket){
	// send packet to server
	if(mypacket.myheader.flags == 0x02) cout << "Send a packet (SYN) to " << hostname << ": " << port_num << endl;
	else if(mypacket.myheader.flags == 0x10) cout << "Send a packet (ACK) to " << hostname << ":" << port_num << endl;
	cout << "\tSend a packet (seq_num = " << mypacket.myheader.seq_num << ", ack_num = " << mypacket.myheader.ack_num << ")\n";
	sendto(sockfd, (struct packet *)&mypacket, sizeof(mypacket), MSG_CONFIRM, (const struct sockaddr *) &servaddr, sizeof(servaddr));

	return;
}

#endif
