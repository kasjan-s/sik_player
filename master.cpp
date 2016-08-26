/* TODO
-wydawanie polecenia „od godz. HH.MM ściągaj z radia X audycję przez M minut”;
  odmierzanie czasu ma być zrealizowane po stronie zawiadowcy;



*/

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

#include "session.h"

#define LINE_SIZE 1000

const std::string START_COMMAND = "START";
const std::string PAUSE_COMMAND = "PAUSE";
const std::string PLAY_COMMAND = "PLAY";
const std::string TITLE_COMMAND = "TITLE";
const std::string QUIT_COMMAND = "QUIT";
const std::string AT_COMMAND = "AT";

std::map<unsigned int, PlayerSession> session_ids;
std::deque<unsigned int> finished_session_ids;
std::vector<int> active_telnet_sessions;
std::mutex mutex;

std::mutex cond_m;
std::condition_variable cv;

void deloader_thread()
{
    // Wait until there's a session to remove
    std::unique_lock<std::mutex> lk(cond_m);
    while(true) {
	    cv.wait(lk, []{return !finished_session_ids.empty();});

	    mutex.lock();
    	// Destroy sessions
    	while (!finished_session_ids.empty()) {
    		unsigned int id = finished_session_ids.front();
    		finished_session_ids.pop_front();
    		if (session_ids.find(id) != session_ids.end()) {
    			session_ids.erase(id);
    		}
	    }
	    mutex.unlock();
 
    	lk.unlock();
	}
}

unsigned int get_new_id() {
	static int id = 0;
	return id++;
}

std::vector<std::string> split_string(const std::string &s, char delim = ' ') {
	std::stringstream ss(s);
	std::string item;
	std::vector<std::string> ret;
	while (getline(ss, item, delim)) {
		ret.push_back(item);
	}

	return ret;
}

bool is_int(std::string str) {
	std::istringstream ss(str);
	int ret; 

	if (!(ss >> ret) || !ss.eof())
		return false;

	return true;
}

int get_int(std::string str) {
	std::istringstream ss(str);
	int ret;

	// Argument should be writable to int without any leftovers
	if (!(ss >> ret) || !ss.eof())
		return -1;

	return ret;
}

int get_int_from_argv(const char* arg) {
	std::istringstream ss(arg);
	int ret;

	// Argument should be writable to int without any leftovers
	if (!(ss >> ret) || !ss.eof())
		return -1;

	return ret;
}

void handle_telnet_iac(std::string& data) {
	static unsigned char iac_byte = 0xFF;
	static unsigned char iac_wwdd[] = {0xFB, 0xFC, 0xFD, 0xFE};

  	for (std::string::iterator it = data.begin(); it != data.end();) {
  		if (*it != iac_byte) {
  			++it;
  			continue;
  		} 
  		// Encountered IAC byte
  		else {
  			it = data.erase(it);

  			// 255 255 sequence
  			if (*it == iac_byte) {
  				++it;
	  			continue;
  			} 

	  		// WILL / WON'T / DO / DON'T sequence
  			else if (std::find(iac_wwdd, iac_wwdd + 4, *it) != iac_wwdd + 4) {
	  			it = data.erase(it);
  				it = data.erase(it);
  				continue;
  			}

	  		// Other sequences
  			else {
  				it = data.erase(it);
  			}
		}
  	}
}

void handle_connection(int conn) {
	char buffer[LINE_SIZE], peeraddr[LINE_SIZE + 1], peername[LINE_SIZE + 1];
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);

	if (getpeername(conn, (struct sockaddr *)&addr, &len) == -1) {
		std::cerr << "Error while performing getpeername" << std::endl;
		return;
	}

	inet_ntop(AF_INET, &addr.sin_addr, peeraddr, LINE_SIZE);
	snprintf(peername, LINE_SIZE, "%s:%d", peeraddr, ntohs(addr.sin_port));

	mutex.lock();
	active_telnet_sessions.push_back(conn);
	mutex.unlock();

	std::string data;
	int rc;

	while (true) {
		memset(buffer, 0, sizeof(buffer));
		rc = read(conn, buffer, sizeof(buffer));

		if (rc == -1) {
			std::cerr << "Error while read" << std::endl;
			return;
		} else if (rc == 0) {
			break;
		}

		data.append(buffer, rc);

		size_t pos;

		// If it finds \r\n, it won't look for \n
		while ((pos = data.find("\r\n")) != std::string::npos
				|| (pos = data.find("\n")) != std::string::npos) {

			std::string line = data.substr(0, pos);

			// Either \r\n was found or \n
			int delimeter_length = data.find("\r\n") != std::string::npos ? 2 : 1;

			data.erase(0, pos + delimeter_length);

			// Parse the extracted line
			handle_telnet_iac(line);
			std::vector<std::string> tokens = split_string(line);
			
			std::string command = tokens[0];

			if (command == START_COMMAND) {
				unsigned int id = get_new_id();
				session_ids.emplace(std::piecewise_construct,
									std::forward_as_tuple(id),
									std::forward_as_tuple(conn, id, active_telnet_sessions, 
										finished_session_ids, tokens, mutex, cv));

				PlayerSession& session = session_ids.at(id);
				if (!session.start()) {
					std::string error = "ERROR popen failed";
					rc = write(conn, error.c_str(), error.size());
					if (rc == -1) {
						std::cerr << "Error while write" << std::endl;
						return;
					}
					continue;
				} else {
					/* Little hack, so remote host actually has the time to make a connection */
					// sleep(1000);

					std::ostringstream ss;
					ss << "OK " << id << std::endl;
					std::string answer = ss.str();
					rc = write(conn, answer.c_str(), answer.size());
				}
			} else if (command == PAUSE_COMMAND ||
					   command == PLAY_COMMAND ||
					   command == QUIT_COMMAND ||
					   command == TITLE_COMMAND) {
				if (tokens.size() != 2 || !is_int(tokens[1])) {
					std::ostringstream ss;
					ss << "ERROR: Proper syntax is \"" << command << " <ID>\"" << std::endl;
					std::string answer = ss.str();
					rc = write(conn, answer.c_str(), answer.size());
				} else {
					unsigned int id = get_int(tokens[1]);

					if (session_ids.find(id) == session_ids.end()) {
						std::ostringstream ss;
						ss << "ERROR: No player with ID " << id << std::endl;
						std::string answer = ss.str();
						rc = write(conn, answer.c_str(), answer.size());
					} else {
						PlayerSession& session = session_ids.at(id);

						if (command == PAUSE_COMMAND)
							session.pause(conn);
						else if (command == PLAY_COMMAND)
							session.play(conn);
						else if (command == TITLE_COMMAND)
							session.title(conn);
						else if (command == QUIT_COMMAND)
							session.quit(conn);
					}
				}
			}
		}

	}

	std::cerr << "Closed connection with " << peername << std::endl;
	close(conn);
	mutex.lock();
	active_telnet_sessions.erase(
		std::remove(active_telnet_sessions.begin(), active_telnet_sessions.end(), conn),
		active_telnet_sessions.end());
	mutex.unlock();
	return;
}

int main(int argc, char* argv[]) {
	int port = 0;

	if (argc > 2) {
		std::cerr << "Wrong number of arguments" << std::endl;
		std::cerr << "Usage: ./master [port]" << std::endl;
		return 1;
	}

	if (argc == 2) {
		if ((port = get_int_from_argv(argv[1])) == -1) {
			std::cerr << "Wrong port number" << std::endl;
			return 1;
		}
	}

	// Creating socket
	int listener;
	if ((listener = socket(PF_INET, SOCK_STREAM, 0))== -1) {
		std::cerr << "Error encountered during socket creation" << std::endl;
		return 1;
	}

	// Bind the socket
	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = port ? htons(port) : 0;
	if (bind(listener, (struct sockaddr *)&server, sizeof(server)) == -1) {
		std::cerr << "Error while performing bind" << std::endl;
		return 1;
	}

	if (!port) {
		socklen_t len = sizeof(server);
		if (getsockname(listener, (struct sockaddr *)&server, &len) == -1) {
			std::cerr << "Error while performing getsockname" << std::endl;
			return 1;
		}

		std::cout << "Listening at port " << (int) ntohs(server.sin_port) << std::endl;
	}

	// Start listening
	if (listen(listener, 10) == -1) {
		std::cerr << "Error while performing listen" << std::endl;
		return 1;
	}

	// Start deloader thread
	std::thread deloader(deloader_thread);

	std::vector<std::thread> telnet_sessions;

	while (true) {
		int msgsock;

		msgsock = accept(listener, (struct sockaddr *) NULL, NULL);
		if (msgsock == -1) {
			std::cerr << "Error while accepting a new telnet connection" << std::endl;
			// TODO: TO return or NOT TO return?
			// return 1;
		}

		telnet_sessions.push_back(std::thread(handle_connection, msgsock));
	}

	return 0;
}