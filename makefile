CC = g++
CLIENT_TEST_FILE = client_test
SERVER_TEST_FILE = server_test
CLIENT_CLASS_FILE = client_class
SERVER_CLASS_FILE = server_class

CLIENT_OUT = client
SERVER_OUT = server

all:
	$(CC) -o $(SERVER_OUT).out $(SERVER_TEST_FILE).cpp $(SERVER_CLASS_FILE).cpp -lpthread
	$(CC) -o $(CLIENT_OUT).out $(CLIENT_TEST_FILE).cpp $(CLIENT_CLASS_FILE).cpp -lpthread
	### example
	### ./server.out
	### ./client.out SERVER_ADDRESS SERVER_PORT ((DIRECTORY_FILE)|(DNS_LOOKUP)|(C_MATH_EXPRESSION)){1,}

clean : 
	rm -f $(CLIENT_OUT).out $(SERVER_OUT).out request_*
