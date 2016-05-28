#include <iostream>
#include <pthread.h>
#include <string>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <regex>

#define BUFFER_SIZE 2000
#define UDP_BUFFER_SIZE 10
#define HEADER_SIZE_LIMIT 1000000

const std::string PLAY_COMMAND = "PLAY";
const std::string PAUSE_COMMAND = "PAUSE";
const std::string TITLE_COMMAND = "TITLE";
const std::string QUIT_COMMAND = "QUIT";

bool play = true;
std::string streamTitle = "TestTitle";
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
    oss << "GET " << path << " HTTP/1.0 \r\n";
    oss << "User-Agent: MPlayer 2.0-728-g2c378c7-4build1\r\n";
    if (md)
        oss << "Icy-MetaData:1 \r\n";
    oss << "\r\n";

    return oss.str();
}

void* udp(void* rport) {
    int* retval = new int;
    *retval = 0;

    int port = *(int*)rport;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    char udpBuffer[UDP_BUFFER_SIZE];
    if (sock < 0) {
        std::cerr << "Couldn't open UDP socket on port " << port << std::endl;
        return 0;
    }

    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *) &server_address,
             (socklen_t) sizeof(server_address)) < 0) {
        std::cerr << "Couldn't bind the UDP socket" << std::endl;
        *retval = 1;
        pthread_exit(retval);
    }

    int snda_len = (socklen_t) sizeof(client_address);
    for (;;) {
        if (quit)
            break;

        int len, flags, sflags;
        socklen_t rcva_len, snd_len;
        do {
            rcva_len = (socklen_t) sizeof(client_address);
            flags = 0;
            len = recvfrom(sock, udpBuffer, sizeof(udpBuffer), flags,
                           (struct sockaddr *) &client_address, &rcva_len);
            if (len < 0) {
                std::cerr << "Error receiving datagram" << std::endl;
                *retval = 1;
                pthread_exit(retval);
            }

            std::string command(udpBuffer, len);

            if (command == PAUSE_COMMAND) {
                play = false;
            } else if (command == PLAY_COMMAND) {
                play = true;
            } else if (command == TITLE_COMMAND) {
                sflags = 0;
                std::string title = streamTitle;
                snd_len = sendto(sock, streamTitle.c_str(), (size_t) title.size(),
                                 sflags, (struct sockaddr *) &client_address, snda_len);
                if (snd_len < 0) {
                    std::cerr << "Error sending datagram" << std::endl;
                    *retval = 1;
                    pthread_exit(retval);
                }
            } else if (command == QUIT_COMMAND) {
                quit = true;
                pthread_exit(retval);
            } else {
                std::cerr << "Ignoring invalid command: " << command << std::endl;
            }
        } while (len >  0);
    }
    pthread_exit(retval);
}

int extract_meta_int(std::string header) {
    std::string METAINTSTR = "icy-metaint:";
    size_t metaint_pos;
    int metadata_int = 0;
    if ((metaint_pos = header.find(METAINTSTR)) == std::string::npos) {
        std::cerr << "Header didn't containt metadata int" << std::endl;
        return -1;
    }

    std::string mdistr1 = header.substr(metaint_pos + METAINTSTR.size());
    std::string mdistr2 = mdistr1.substr(0, mdistr1.find("\r\n"));

    std::istringstream ss(mdistr2);
    if (!(ss >> metadata_int)) {
        std::cerr << "Wrong metaint in header " << ss.str() << std::endl;
        return -1;
    }

    return metadata_int;
}

int parseMetadata(std::string metadata) {
    if (metadata.size()) {
        std::cerr << "Metadata size " << metadata.size() << std::endl;
        std::cerr << metadata << std::endl;

        std::string titlebeg = "StreamTitle='";
        std::string titleend = "';";
        size_t beg = metadata.find(titlebeg);
        size_t end = metadata.find(titleend);

        if (beg == std::string::npos || end == std::string::npos) 
            return -1;


        size_t pos = beg + titlebeg.size();
        std::string strTitle = metadata.substr(pos, end- pos);

        streamTitle = strTitle;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 7) {
        // ./player host path r-port file m-port md
        displayHelp();
        return  1;
    }

    int sock;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;

    int  err;
    char buffer[BUFFER_SIZE];
    ssize_t len, rcv_len;

    int m_port;

    std::istringstream ss(argv[5]);
    if (!(ss >> m_port)) {
        std::cerr << "Invalid port number " << argv[5] << std::endl;
        return 1;
    }

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

    freeaddrinfo(addr_result);

    // set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        std::cerr << "Setsockopt failed" << std::endl;
        return 1;
    }

    // check metadata option
    std::string mdstr = argv[6];
    if (mdstr != "yes" && mdstr != "no") {
        std::cout << "Invalid option for metadata. Write 'yes' or 'no'." << std::endl;
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

    // start UDP thread, that's listening for incomming commands
    pthread_t udp_thread;

    if (pthread_create(&udp_thread, NULL, udp, &m_port)) {
        std::cerr << "Error creating thread" << std::endl;
        return 1;
    }

    // get and parse header
    std::string header;
    std::string partial_data;
    bool header_end = false;
    int metadata_int = -1;
    while (!header_end) {
        if (quit || header_end)
            break;

        memset(buffer, 0, sizeof(buffer));
        rcv_len = read(sock, buffer, sizeof(buffer) - 1);
        if (rcv_len < 0) {
            std::cerr << "Error receiving from server" << std::endl;
            pthread_cancel(udp_thread);
            return 1;
        }

        header.append(buffer, rcv_len);

        std::string DELIMETER = "\r\n";
        size_t pos;
        while ((pos = header.find(DELIMETER)) != std::string::npos) {
            std::string header_line = header.substr(0, pos);
            header.erase(0, pos + DELIMETER.size());

            // parse header line
            if (header_line.length() == 0) {
                header_end = true;
            } else if (header_line.find("icy-metaint:") != std::string::npos) {
                if (md) {
                    metadata_int = extract_meta_int(header_line);

                    if (metadata_int < 0) {
                        pthread_cancel(udp_thread);
                        return 1;
                    }
                }
            } else {
                std::cerr << "Ignoring: " << header_line << std::endl;
            }
        }
    }


    if (md && metadata_int < 0) {
        std::cerr << "Header did not contain the requested metaint" << std::endl;
        pthread_cancel(udp_thread);
        return 1;
    }
    std::cerr << "Header loaded" << std::endl;
    std::cerr << metadata_int << std::endl;

    // leftover data that might have been received with header frames
    size_t bytes_read = header.size();
    if (play)
        std::cout.write(header.c_str(), header.size());

    std::string metadata;
    for (;;) {
        if (quit)
            break;

        // Read from sock
        memset(buffer, 0, sizeof(buffer));
        rcv_len = read(sock, buffer, sizeof(buffer) - 1);
        if (rcv_len < 0) {
            std::cerr << "Error receiving from server" << std::endl;
            pthread_cancel(udp_thread);
            return 1;
        }

        bytes_read += rcv_len;

        if (bytes_read <= (size_t) metadata_int) {
            // Didn't reach metadata part yet
            if (play)
                std::cout.write(buffer, rcv_len);
        } else {
            // Write the rest of the audio
            size_t data_len = rcv_len - (bytes_read - metadata_int);
            if (play)
                std::cout.write(buffer, data_len);

            // Read metadata lenght byte
            size_t md_len = bytes_read - metadata_int - 1;
            int metadata_length = static_cast<int>(buffer[data_len]) * 16;

            std::string metadata(buffer + data_len +1, md_len);

            // Receive and parse metadata
            while (metadata.size() < (size_t) metadata_length) {
                if (quit)
                    break;

                memset(buffer, 0, sizeof(buffer));
                rcv_len = read(sock, buffer, sizeof(buffer) - 1);
                if (rcv_len < 0) {
                    std::cerr << "Error receiving from server" << std::endl;
                    pthread_cancel(udp_thread);
                    return 1;
                }

                metadata.append(buffer, rcv_len);
            }

            if (quit)
                break;

            // Write the extra data
            std::string audio = metadata.substr(metadata_length);
            if (play)
                std::cout.write(audio.c_str(), audio.size());
            bytes_read = audio.size();

            // Parse metadataa
            metadata = metadata.substr(0, metadata_length);
            if (parseMetadata(metadata) < 0) {
                std::cerr << "Metadata doesn't contain StreamTitle" << std::endl;
                pthread_cancel(udp_thread);
                return 1;
            }
        }

    }

    std::cout.flush();
    (void) close(sock); // socket would be closed anyway when the program ends

    void *status;
    if (pthread_join(udp_thread, &status)) {
        std::cerr << "Error joining threads" << std::endl;
        return 1;
    }

    int exit_code = *((int*)status);
    delete (int*)status;
    return exit_code;

}
