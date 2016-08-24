#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

#define LINE_SIZE 1000

int get_int_from_argv(const char* arg) {
	std::istringstream ss(arg);
	int ret;

	// Argument should be writable to int without any leftovers
	if (!(ss >> ret) || !ss.eof())
		return -1;

	return ret;
}

void handle_connection(int conn) {
	char line[LINE_SIZE + 1], peeraddr[LINE_SIZE + 1], peername[LINE_SIZE + 1];
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);

	if (getpeername(conn, (struct sockaddr *)&addr, &len) == -1) {
		std::cerr << "Error while performing getpeername" << std::endl;
		return;
	}

	inet_ntop(AF_INET, &addr.sin_addr, peeraddr, LINE_SIZE);
	snprintf(peername, LINE_SIZE, "%s:%d", peeraddr, ntohs(addr.sin_port));

	int rc;
	while (true) {
		memset(line, 0, sizeof(line));
		rc = read(conn, line, sizeof(line) - 1);

		if (rc == -1) {
			std::cerr << "Error while read" << std::endl;
			return;
		} else if (rc == 0) {
			break;
		}

		rc = write(conn, line, sizeof(line) - 1);
		std::cout << line;
	}

	std::cerr << "Closed connection with " << peername << std::endl;
	close(conn);
	return;
}

int main(int argc, char* argv[]) {
	int port = 0;

	if (argc > 2) {
		std::cerr << "Wrong number of arguments" << std::endl;
		std::cerr << "Usage: ./master [port]" << std::endl;
		return -1;
	}

	if (argc == 2) {
		if ((port = get_int_from_argv(argv[1])) == -1) {
			std::cerr << "Wrong port number" << std::endl;
			return -1;
		}
	}

	// Creating socket
	int listener;
	if ((listener = socket(PF_INET, SOCK_STREAM, 0))== -1) {
		std::cerr << "Error encountered during socket creation" << std::endl;
		return -1;
	}

	// Bind the socket
	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = port ? htons(port) : 0;
	if (bind(listener, (struct sockaddr *)&server, sizeof(server)) == -1) {
		std::cerr << "Error while performing bind" << std::endl;
		return -1;
	}

	if (!port) {
		socklen_t len = sizeof(server);
		if (getsockname(listener, (struct sockaddr *)&server, &len) == -1) {
			std::cerr << "Error while performing getsockname" << std::endl;
			return -1;
		}

		std::cout << "Listening at port " << (int) ntohs(server.sin_port) << std::endl;
	}

	// Start listening
	if (listen(listener, 10) == -1) {
		std::cerr << "Error while performing listen" << std::endl;
		return -1;
	}

	std::vector<std::thread> telnet_sessions;

	while (true) {
		int msgsock;

		msgsock = accept(listener, (struct sockaddr *) NULL, NULL);
		if (msgsock == -1) {
			std::cerr << "Error while performing accept" << std::endl;
			// TODO: proper cleanup
			return -1;
		}

		telnet_sessions.push_back(std::thread(handle_connection, msgsock));
	}


	return 0;
}