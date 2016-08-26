#include "session.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>

#define BUFFER_SIZE 1000

bool PlayerSession::start() {
	std::ostringstream ss;
  	ss << "ssh ";
  	ss << parameters[1] << " \"player ";

	for(size_t i = 2; i < parameters.size(); ++i)
	{
  		if(i != 2)
    		ss << " ";
  		ss << parameters[i];
	}

	ss << "\" 3>&2 2>&1 1>&3";

	std::string ssh_string = ss.str();

	descriptor = popen(ssh_string.c_str(), "r");

	if (descriptor == NULL)
		return false;

	the_thread = std::thread(&PlayerSession::main_thread, this);

	return true;
}

void PlayerSession::main_thread() {
	char buffer[BUFFER_SIZE];

	std::ostringstream ss;
	/* player only writes to stderr in case of error, so this thread will block
	   here till actual error happens */
	if (fgets(buffer, BUFFER_SIZE, descriptor) == NULL && !stop_thread) {
		ss << "ERROR " << id << " error while trying to read player stderr" << std::endl;

		std::string error = ss.str();

		send_msg(connection_descriptor, error);
	}
	
	pclose(descriptor);
	return;
}

void PlayerSession::send_msg(int cdescriptor, std::string str) {
	int rc;
	mutex.lock();
	if(std::find(active_connections.begin(),
				 active_connections.end(), 
				 cdescriptor) != active_connections.end()) {

		mutex.unlock();
		rc = write(cdescriptor, str.c_str(), str.size());

		if (rc == -1) {
			std::cerr << "Error while write" << std::endl;
			return;
		}
	} else {
		mutex.unlock();
	}
}

void PlayerSession::pause(int cdescriptor) {
	std::ostringstream ss;
	if (send_datagram("PAUSE")) {
		ss << "OK " << id << std::endl;
	} else {
		ss << "ERROR " << id << " failed to send PAUSE command" << std::endl;
	}
	std::string answer = ss.str();
	send_msg(cdescriptor, answer);
}

void PlayerSession::quit(int cdescriptor) {
	std::ostringstream ss;
	stop_thread = true;
	if (send_datagram("QUIT")) {
		ss << "OK " << id << std::endl;
	} else {
		ss << "ERROR " << id << " failed to send PAUSE command" << std::endl;
	}
	std::string answer = ss.str();
	send_msg(cdescriptor, answer);
}

bool PlayerSession::send_datagram(std::string msg) {
	int rc, sock;
	size_t len;
	socklen_t rcva_len;

	struct addrinfo addr_hints;
	struct addrinfo* addr_result;

	struct sockaddr_in my_address;

	addr_hints.ai_family = AF_INET;
  	addr_hints.ai_socktype = SOCK_DGRAM;
  	addr_hints.ai_protocol = IPPROTO_UDP;
  	addr_hints.ai_flags = 0;
  	addr_hints.ai_addrlen = 0;
  	addr_hints.ai_addr = NULL;
  	addr_hints.ai_canonname = NULL;
  	addr_hints.ai_next = NULL;

  	rc = getaddrinfo(pc.c_str(), NULL, &addr_hints, &addr_result);
  	if (rc != 0) { // system error
    	std::cerr << "getaddrinfo" << std::endl;
  	}

 	my_address.sin_family = AF_INET; // IPv4
  	my_address.sin_addr.s_addr = 
  		((struct sockaddr_in*) (addr_result->ai_addr))->sin_addr.s_addr; // address IP
  	my_address.sin_port = htons((uint16_t) atoi(mport.c_str())); // port from the command line

  	rcva_len = (socklen_t) sizeof(my_address);

  	freeaddrinfo(addr_result);

  	sock = socket(PF_INET, SOCK_DGRAM, 0);
  	if (sock < 0)
  		std::cerr << "socket" << std::endl;

  	len = sendto(sock, msg.c_str(), msg.size(), 0, 
  			(struct sockaddr *) &my_address, rcva_len);

  	if (len != msg.size()) {
  		std::cerr << "Error, partial sendto" << std::endl;
  	}

  	return true;
}