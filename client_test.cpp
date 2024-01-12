#include "client_class.hpp"

// for multi thread
#include <pthread.h>

typedef void * (*thread_ptr)(void *);

using namespace std;

// test for client
int main(int argc, char *argv[]) {

	vector<string> requests;
	char *hostname = (char *) malloc(20);
	strcpy(hostname, argv[1]);
	int port = atoi(argv[2]);

	for(int i=3;i<argc;i++){
		requests.push_back(argv[i]);
	}

	int client_num = 1;

	client_class* myClient[client_num];

	for(int i=0;i<client_num;i++){
		(myClient[i]) = new client_class(hostname, port);
	}

	pthread_t request_thread[100];
	cout << "client build complete\n";

	// start sending message
	for(int i=0;i<client_num;i++){

		// set the sending packet
		for(int p=0;p<requests.size();p++){
			myClient[i]->set_request(requests[p]);
		}

		if(pthread_create(&request_thread[i],
							NULL,
							(thread_ptr)&client_class::processing, 	// pointer to member function
							myClient[i] 							//the object
							) != 0)
			cout << "pthread create failed: " << i << "th\n";
	}

	int i = 0;
	while(i < client_num) pthread_join(request_thread[i++],NULL);
	
	return 0;
}
