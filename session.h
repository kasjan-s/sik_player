#ifndef _SIKSESSION_
#define _SIKSESSION_

#include <atomic>
#include <thread>
#include <vector>

class PlayerSession {
public:
	PlayerSession(int conn, std::vector<std::string> parameters) 
	: connection_descriptor(conn), 
	  stop_thread(false), 
	  the_thread() {}
	~PlayerSession() {
		stop_thread = true;
		the_thread.join();
	}
	void start();
private:
	int connection_descriptor;

	std::atomic_bool stop_thread;
	std::thread the_thread;

	std::string host;
	std::string path;
	unsigned int rport;
	std::string file_path;
	unsigned int mport;
	std::string md;
};

#endif