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
#define TITLE_TIMEOUT_SECONDS 3

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

	/* Redirecting stderr to stdout, so we can read error msgs.
	   No need to keep stdout as we don't need to implement '-' as file option */
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
	   here till actual error happens
	   However if it can also return fail if we command player to quit,
	   but then stop_thread will have value true */
	if ((fgets(buffer, BUFFER_SIZE, descriptor) == NULL) && !stop_thread) {
		ss << "ERROR " << id << " error while trying to read player stderr" << std::endl;

		std::string error = ss.str();

		mutex.lock();
		send_msg(connection_descriptor, error);
		mutex.unlock();
	}

	pclose(descriptor);

	mutex.lock();
	finished_sessions.push_back(id);

	if (quit_descriptor != -1) {
		ss.clear();
		ss << "OK " << id << " player quit" << std::endl;
		std::string answer = ss.str();
		send_msg(quit_descriptor, answer);
	}

	cond_var.notify_one();
	mutex.unlock();
}

void PlayerSession::send_msg(int cdescriptor, std::string str) {
	int rc;
	if(std::find(active_connections.begin(),
				 active_connections.end(), 
				 cdescriptor) != active_connections.end()) {

		rc = write(cdescriptor, str.c_str(), str.size());

		if (rc == -1) {
			std::cerr << "Error while write" << std::endl;
			return;
		}
	} 
}

void PlayerSession::pause(int cdescriptor) {
	std::ostringstream ss;
	if (send_datagram("PAUSE")) {
		ss << "OK " << id << std::endl;
	} else {
		ss << error_msg;
		error_msg = "";
	}
	std::string answer = ss.str();
	mutex.lock();
	send_msg(cdescriptor, answer);
	mutex.unlock();
}

void PlayerSession::play(int cdescriptor) {
	std::ostringstream ss;
	if (send_datagram("PLAY")) {
		ss << "OK " << id << std::endl;
	} else {
		ss << error_msg;
		error_msg = "";
	}
	std::string answer = ss.str();
	mutex.lock();
	send_msg(cdescriptor, answer);
	mutex.unlock();
}

void PlayerSession::title(int cdescriptor) {
	std::string title_str;
	std::ostringstream ss;
	if (send_datagram("TITLE", title_str)) {
		ss << "OK " << id << " " << title_str << std::endl;
	} else {
		ss << error_msg;
		error_msg = "";
	}
	std::string answer = ss.str();
	mutex.lock();
	send_msg(cdescriptor, answer);
	mutex.unlock();
}

void PlayerSession::quit(int cdescriptor) {
	std::ostringstream ss;
	if (stop_thread) {
		ss << "ERROR " << id << " QUIT already called" << std::endl;
		std::string answer = ss.str();
		mutex.lock();
		send_msg(cdescriptor, answer);
		mutex.unlock();
		return;
	}

	stop_thread = true;
	quit_descriptor = cdescriptor;
	if (!send_datagram("QUIT")) {
		ss << error_msg;
		error_msg = "";
		std::string answer = ss.str();
		mutex.lock();
		send_msg(cdescriptor, answer);
		mutex.unlock();
	}
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
  	if (rc != 0) {
		std::ostringstream ss;
		ss << "ERROR " << id << " getaddrinfo() failed" << std::endl;
		error_msg = ss.str();
		return false;
  	}

 	my_address.sin_family = AF_INET; // IPv4
  	my_address.sin_addr.s_addr = 
  		((struct sockaddr_in*) (addr_result->ai_addr))->sin_addr.s_addr; // address IP
  	my_address.sin_port = htons((uint16_t) atoi(mport.c_str())); // port from the command line

  	rcva_len = (socklen_t) sizeof(my_address);

  	freeaddrinfo(addr_result);

  	sock = socket(PF_INET, SOCK_DGRAM, 0);
  	if (sock < 0) {
		std::ostringstream ss;
		ss << "ERROR " << id << " socket() failed" << std::endl;
		error_msg = ss.str();
		return false;
  	}

  	len = sendto(sock, msg.c_str(), msg.size(), 0, 
  			(struct sockaddr *) &my_address, rcva_len);

  	if (len != msg.size()) {
		std::ostringstream ss;
		ss << "ERROR " << id << " partial sendto()" << std::endl;
		error_msg = ss.str();
		return false;
  	}

  	close(sock);

  	return true;
}

bool PlayerSession::send_datagram(std::string msg, std::string& response) {
	int rc, sock;
	size_t len;
	socklen_t rcva_len;

	struct addrinfo addr_hints;
	struct addrinfo* addr_result;

	struct sockaddr_in my_address;
	struct sockaddr_in srvr_address;

	addr_hints.ai_family = AF_INET;
  	addr_hints.ai_socktype = SOCK_DGRAM;
  	addr_hints.ai_protocol = IPPROTO_UDP;
  	addr_hints.ai_flags = 0;
  	addr_hints.ai_addrlen = 0;
  	addr_hints.ai_addr = NULL;
  	addr_hints.ai_canonname = NULL;
  	addr_hints.ai_next = NULL;

  	rc = getaddrinfo(pc.c_str(), NULL, &addr_hints, &addr_result);
  	if (rc != 0) { 
		std::ostringstream ss;
		ss << "ERROR " << id << " getaddrinfo() failed" << std::endl;
		error_msg = ss.str();
		return false;
  	}

 	my_address.sin_family = AF_INET; // IPv4
  	my_address.sin_addr.s_addr = 
  		((struct sockaddr_in*) (addr_result->ai_addr))->sin_addr.s_addr; // address IP
  	my_address.sin_port = htons((uint16_t) atoi(mport.c_str())); // port from the command line

  	rcva_len = (socklen_t) sizeof(my_address);

  	freeaddrinfo(addr_result);

  	sock = socket(PF_INET, SOCK_DGRAM, 0);
  	if (sock < 0) {
		std::ostringstream ss;
		ss << "ERROR " << id << " socket() failed" << std::endl;
		error_msg = ss.str();
		return false;
  	}

  	len = sendto(sock, msg.c_str(), msg.size(), 0, 
  			(struct sockaddr *) &my_address, rcva_len);

  	if (len != msg.size()) {
		std::ostringstream ss;
		ss << "ERROR " << id << " partial sendto()" << std::endl;
		error_msg = ss.str();
		return false;
  	}


	struct timeval tv;
	tv.tv_sec = TITLE_TIMEOUT_SECONDS;
	tv.tv_usec = 0;
	
	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		std::ostringstream ss;
		ss << "ERROR " << id << " setsockopt()" << std::endl;
		error_msg = ss.str();
		return false;
	}

  	char buffer[BUFFER_SIZE];
  	len = recvfrom(sock, buffer, sizeof(buffer), 0,
  				(struct sockaddr *) &srvr_address, &rcva_len);

  	if (len < 0) {
		std::ostringstream ss;
		ss << "ERROR " << id << " timed out while waiting for Title answer (or other error)" << std::endl;
		error_msg = ss.str();
		return false;
  	}

  	response = std::string(buffer);

  	close(sock);

  	return true;
}