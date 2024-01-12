#include "server_class.hpp"

using namespace std;

// test for server
int main() {

	server_class myServer;
	cout << "server built complete\n";

	myServer.welcome_sock_listen();

	return 0;
}
