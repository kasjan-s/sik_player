#include <boost/regex.hpp>
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define BUFFER_SIZE 10000
#define UDP_BUFFER_SIZE 10

const std::string PLAY_COMMAND = "PLAY";
const std::string PAUSE_COMMAND = "PAUSE";
const std::string TITLE_COMMAND = "TITLE";
const std::string QUIT_COMMAND = "QUIT";


pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
bool paused = false;
std::string stream_title = "";
bool quit = false;

void displayHelp() {
    std::cout << "Usage: ./player host path r-port file m-port md" << std::endl;
    std::cout << "\thost     - server name" << std::endl;
    std::cout << "\tpath     - resource path" << std::endl;
    std::cout << "\tr-port   - server port" << std::endl;
    std::cout << "\tfile     - file name to which stream should be saved (or - for stdout)" << std::endl;
    std::cout << "\tm-port   - command port" << std::endl;
    std::cout << "\tmd       - require metadata" << std::endl;
}

std::string get_request(std::string path, bool md) {
    std::ostringstream oss;
    oss << "GET " << path << " HTTP/1.0\r\n";
    oss << "User-Agent: MPlayer 2.0-728-g2c378c7-4build1\r\n";
    if (md)
        oss << "Icy-MetaData:1\r\n";
    oss << "\r\n";

    return oss.str();
}

bool is_true_safe(bool &val) {
    bool ret;
    pthread_mutex_lock(&mutex);
    ret = val;
    pthread_mutex_unlock(&mutex);
    return ret;
}

void* udp(void* rport) {
    int* retval = new int;
    *retval = 0;

    int port = *(int*)rport;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    char udpBuffer[UDP_BUFFER_SIZE];
    if (sock < 0) {
        std::cerr << "Couldn't open UDP socket on port " << port << std::endl;
        pthread_mutex_lock(&mutex);
        quit = true;
        pthread_mutex_unlock(&mutex);
        *retval = 1;
        pthread_exit(retval);
    }

    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *) &server_address,
             (socklen_t) sizeof(server_address)) < 0) {
        std::cerr << "Couldn't bind the UDP socket" << std::endl;
        pthread_mutex_lock(&mutex);
        quit = true;
        pthread_mutex_unlock(&mutex);
        *retval = 1;
        pthread_exit(retval);
    }

    int snda_len = (socklen_t) sizeof(client_address);
    int len, flags, sflags;
    socklen_t rcva_len, snd_len;
    do {
        if (is_true_safe(quit))
            break;

        rcva_len = (socklen_t) sizeof(client_address);
        flags = 0;
        len = recvfrom(sock, udpBuffer, sizeof(udpBuffer), flags,
                       (struct sockaddr *) &client_address, &rcva_len);
        if (len < 0) {
            std::cerr << "Error receiving datagram" << std::endl;
            pthread_mutex_lock(&mutex);
            quit = true;
            pthread_mutex_unlock(&mutex);
            *retval = 1;
            pthread_exit(retval);
        }

        std::string command(udpBuffer, len);

        if (command == PAUSE_COMMAND) {
            pthread_mutex_lock(&mutex);
            paused = true;
            pthread_mutex_unlock(&mutex);
        } else if (command == PLAY_COMMAND) {
            pthread_mutex_lock(&mutex);
            paused = false;
            pthread_mutex_unlock(&mutex);
        } else if (command == TITLE_COMMAND) {
            sflags = 0;
            pthread_mutex_lock(&mutex);
            std::string title = stream_title;
            pthread_mutex_unlock(&mutex);
            snd_len = sendto(sock, title.c_str(), (size_t) title.size(),
                             sflags, (struct sockaddr *) &client_address, snda_len);
            if (snd_len < 0) {
                pthread_mutex_lock(&mutex);
                quit = true;
                pthread_mutex_unlock(&mutex);
                std::cerr << "Error sending datagram" << std::endl;
                *retval = 1;
                pthread_exit(retval);
            }
        } else if (command == QUIT_COMMAND) {
            pthread_mutex_lock(&mutex);
            quit = true;
            pthread_mutex_unlock(&mutex);
            pthread_exit(retval);
        } else {
            std::cerr << "Ignoring invalid command: " << command << std::endl;
        }
    } while (len >= 0);
    pthread_exit(retval);
}

int extract_meta_int(std::string header_line) {
    static const std::string METAINTSTR = "icy-metaint:";
    size_t metaint_pos;
    int metadata_int = 0;
    if ((metaint_pos = header_line.find(METAINTSTR)) == std::string::npos) {
        std::cerr << "Header didn't containt metadata int" << std::endl;
        return -1;
    }

    std::string mdistr1 = header_line.substr(metaint_pos + METAINTSTR.size());
    std::string mdistr2 = mdistr1.substr(0, mdistr1.find("\r\n"));

    std::istringstream ss(mdistr2);
    if (!(ss >> metadata_int)) {
        std::cerr << "Wrong metaint in header " << ss.str() << std::endl;
        return -1;
    }

    return metadata_int;
}

int parse_metadata(std::string metadata) {
    static std::string TITLE_REGEX = "^.*StreamTitle='(.*?)';.*$";

    if (metadata.size()) {
        boost::regex re;
        boost::cmatch matches;
        re.assign(TITLE_REGEX);
        if (boost::regex_match(metadata.c_str(), matches, re)) {
            pthread_mutex_lock(&mutex);
            stream_title = matches[1];
            pthread_mutex_unlock(&mutex);
        } else {
            return -1;
        }
    }

    return 0;
}

int get_int_from_argv(const char* arg) {
    std::istringstream ss(arg);
    int ret;
    
    // Argument has to be simply an integer number - should be writable to int without any leftovers
    if (!(ss >> ret) || !ss.eof()) {
        return -1;
    }

    return ret;
}

int main(int argc, char *argv[])
{
    if (argc != 7) {
        std::cerr << "Wrong number of arguments" << std::endl;
        displayHelp();
        return  1;
    }

    int sock;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;

    int  err;
    char buffer[BUFFER_SIZE];
    ssize_t len, rcv_len;

    int m_port = get_int_from_argv(argv[5]);
    if (m_port <= 0 || m_port > 65535) {
        std::cerr << "Invalid m-port number" << std::endl;
        return 1;
    }

    int r_port = get_int_from_argv(argv[3]);
    if (r_port <= 0 || r_port > 65535) {
        std::cerr << "Invalid r-port number" << std::endl;
        return 1;
    }

    std::streambuf *buf;
    std::ofstream outfile;

    if (std::string(argv[4]) != "-") {
        outfile.open(std::string(argv[4], std::ofstream::trunc | std::ofstream::out));
        buf = outfile.rdbuf();
    } else {
        buf = std::cout.rdbuf();
    }

    std::ostream output(buf);

    // 'converting' host/port in string to struct addrinfo
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    err = getaddrinfo(argv[1], argv[3], &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) { // system error
        std::cerr << "Systerm error during getaddrinfo: %s" << gai_strerror(err) << std::endl;
        return 1;
    }
    else if (err != 0) { // other error (host not found, etc.)
        std::cerr << "Error during getaddrinfo: %s" << gai_strerror(err) << std::endl;
        return 1;
    }

    // initialize socket according to getaddrinfo results
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0) {
        std::cerr << "Error during socket call" << std::endl;
        return 1;
    }

    // connect socket to the server
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        std::cerr << "Error during connect" << std::endl;
        return 1;
    }

    // set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        std::cerr << "Setsockopt failed" << std::endl;
        return 1;
    }

    freeaddrinfo(addr_result);

    // check metadata option
    std::string mdstr = argv[6];
    if (mdstr != "yes" && mdstr != "no") {
        std::cerr << "Invalid option for metadata. Write 'yes' or 'no'." << std::endl;
        return 1;
    }
    bool md = mdstr == "yes";

    // send GET request
    std::string request = get_request(argv[2], md);

    len = request.size();
    if (write(sock, request.c_str(), len) != len) {
        std::cerr << "Error sending GET request" << std::endl;
        return 1;
    }

    // start UDP thread for incomming commands
    pthread_t udp_thread;

    if (pthread_create(&udp_thread, NULL, udp, &m_port)) {
        std::cerr << "Error creating thread" << std::endl;
        return 1;
    }

    // get and parse header
    std::string header;
    std::string partial_data;
    bool header_end = false;
    bool first_line = true;
    int metadata_int = -1;
    while (!header_end && !is_true_safe(quit)) {
        memset(buffer, 0, sizeof(buffer));
        rcv_len = read(sock, buffer, sizeof(buffer) - 1);
        if (rcv_len < 0) {
            pthread_cancel(udp_thread);
            std::cerr << "Error while receiving header from server" << std::endl;
            return 1;
        }

        header.append(buffer, rcv_len);

        static const std::string DELIMETER = "\r\n";
        size_t pos;
        while ((pos = header.find(DELIMETER)) != std::string::npos) {
            std::string header_line = header.substr(0, pos);
            header.erase(0, pos + DELIMETER.size());

            // Parse header line 
	    
	    // First line of the header - status code
            if (first_line) {
                first_line = false;
                static std::string STATUS_REGEX = "^ICY (\\d{3}) (.*)$";

                boost::regex re;
                boost::cmatch matches;
                re.assign(STATUS_REGEX);
                if (boost::regex_match(header_line.c_str(), matches, re)) {
                    if (matches[1] == "200")
                        continue;
                    std::cerr << "Response code " << matches[2] << ": " << matches[3] << std::endl;
                    pthread_cancel(udp_thread);
                    return 1;
                } else {
                    std::cerr << "Server did not answer with ICY response" << std::endl;
                    pthread_cancel(udp_thread);
                    return 1;
                }
            } 
	    // Empty line - end of header
	    else if (header_line.length() == 0) {
                header_end = true;
	    // Metaint line
            } else if (header_line.find("icy-metaint:") != std::string::npos) {
                if (md) {
                    metadata_int = extract_meta_int(header_line);

                    if (metadata_int < 0) {
                        pthread_cancel(udp_thread);
                        return 1;
                    }
                } else {
			std::cerr << "Server provided metaint, though it was not requested" << std::endl;
			pthread_cancel(udp_thread);
			return 1;
		}
            }
        }
    }

    if (md && metadata_int < 0) {
        std::cerr << "Header did not contain the requested metaint" << std::endl;
        pthread_cancel(udp_thread);
        return 1;
    }

    // leftover data that might have been received with header frames
    size_t bytes_read = header.size();
    if (!is_true_safe(paused)) {
        output.write(header.c_str(), header.size());
        output.flush();
    }

    std::string metadata;
    while (!is_true_safe(quit)) {
        // Read from sock
        memset(buffer, 0, sizeof(buffer));
        rcv_len = read(sock, buffer, sizeof(buffer) - 1);
        if (rcv_len < 0) {
            std::cerr << "Error receiving data from server " << errno <<  std::endl;
            pthread_cancel(udp_thread);

            return 1;
        } else if (rcv_len == 0) {
            pthread_cancel(udp_thread);
            return 0;
        }

        bytes_read += rcv_len;

        if (bytes_read <= (size_t) metadata_int) {
            // Didn't reach metadata part yet
            if (!is_true_safe(paused)) {
                output.write(buffer, rcv_len);
                output.flush();
            }
        } else {
            // Write the rest of the audio
            size_t data_len = rcv_len - (bytes_read - metadata_int);
            if (!is_true_safe(paused)) {
                output.write(buffer, data_len);
                output.flush();
            }

            // Read metadata lenght byte
            size_t md_len = bytes_read - metadata_int - 1;
            int metadata_length = static_cast<int>(buffer[data_len]) * 16;

            std::string metadata(buffer + data_len +1, md_len);

            // Receive and parse metadata
            while (metadata.size() < (size_t) metadata_length) {
                if (is_true_safe(quit))
                    break;

                memset(buffer, 0, sizeof(buffer));
                rcv_len = read(sock, buffer, sizeof(buffer) - 1);
                if (rcv_len < 0) {
                    // Timeouts aren't allowed while receiving metadata
                    std::cerr << "Error while receiving metadata" << std::endl;
                    std::cerr << metadata << " " << metadata.size() << " " << metadata_length <<
                        " " << errno << std::endl;
                    pthread_cancel(udp_thread);
                    return 1;
                }

                metadata.append(buffer, rcv_len);
            }

            if (is_true_safe(quit))
                break;

            // Write the extra data
            std::string audio = metadata.substr(metadata_length);
            if (!is_true_safe(paused)) {
                output.write(audio.c_str(), audio.size());
                output.flush();
            }
            bytes_read = audio.size();

            // Parse metadataa
            metadata = metadata.substr(0, metadata_length);
            if (parse_metadata(metadata) < 0) {
                std::cerr << "Metadata doesn't contain StreamTitle" << std::endl;
                pthread_cancel(udp_thread);
                return 1;
            }
        }
    }

    (void) close(sock); // socket would be closed anyway when the program ends

    if (outfile.is_open())
        outfile.close();

    void *status;
    if (pthread_join(udp_thread, &status)) {
        std::cerr << "Error joining threads" << std::endl;
        return 1;
    }

    int exit_code = *((int*)status);
    delete (int*)status;
    return exit_code;
}
