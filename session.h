#ifndef _SIKSESSION_
#define _SIKSESSION_

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

class PlayerSession {
public:
	PlayerSession(int conn, unsigned int i, 
		std::vector<int>& ac, std::deque<unsigned int>& f, 
		std::vector<std::string> p, std::mutex& mtx,
		std::condition_variable& cv) 
	: connection_descriptor(conn), 
	  id(i),
	  active_connections(ac),
	  finished_sessions(f),
	  parameters(p),
	  mutex(mtx),
	  cond_var(cv),
	  stop_thread(false), 
	  the_thread(),
	  pc(parameters[1]),
	  mport(parameters[6]) {}
	~PlayerSession() {
		stop_thread = true;
		the_thread.join();
	}
	bool start();
	void send_msg(int cdescriptor, std::string str);
	void pause(int cdescriptor);
	void play(int cdescriptor);
	void title(int cdescriptor);
	void quit(int cdescriptor);
private:
	int connection_descriptor;
	unsigned int id;
	std::vector<int>& active_connections;
	std::deque<unsigned int>& finished_sessions;

	/*
		0 - START
		1 - pc
		2 - host
		3 - path
		4 - r-port
		5 - file
		6 - m-port
		7 - md
	*/
	std::vector<std::string> parameters;

	std::mutex& mutex;
	std::condition_variable& cond_var;
	std::atomic_bool stop_thread;	
	std::thread the_thread;

	std::string pc;
	std::string mport;
	FILE* descriptor;

	bool send_datagram(std::string str);
	bool send_datagram(std::string str, std::string& response);
	void main_thread();
	std::string error_msg;
};

#endif