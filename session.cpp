#include "session.h"

#include <algorithm>
#include <iostream>
#include <sstream>

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
	if (fgets(buffer, BUFFER_SIZE, descriptor) == NULL) {
		ss << "ERROR " << id << " error while trying to read player stderr" << std::endl;
	} else {
		ss << "ERROR " << id << " " << buffer << std::endl;
	}

	std::string error = ss.str();

	send_msg(connection_descriptor, error);

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