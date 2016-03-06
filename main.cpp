#include <iostream>
#include <sys/select.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <boost/shared_ptr.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "member.h"

using namespace std;

const int IPSTRSZ = 32;

void
readInput(int argc, char **argv, MemberCfg &cfg)
{
    int opt;
    char ip[IPSTRSZ];
    char port[16];
    char localport[16];
    char log[32];
    int timeout;
    uint32_t uuid;
    int bflag = 0, pflag=0, lflag=0, uflag=0, tflag =0;

    bzero(&cfg, sizeof(MemberCfg));

    //read from command line, config file or stdin
    //for now hardcode
    while((opt = getopt(argc, argv, "b:p:t:u:l:")) != -1) {
        switch(opt) {

            case 'b':
            {
                //bootstrap IP
                //any member of the cluster can bootstrap 
                //a new member
                bflag =1;
                bzero(ip, IPSTRSZ);
                strncpy(ip, optarg, IPSTRSZ);
                break;
            }

            case 'p':
            {
                pflag =1;
                //listening port on bootstrap host
                bzero(port, 16);
                strncpy(port, optarg, 16);
                break;
            }
    
            case 't':
            {
                tflag =1;
                //listening port on bootstrap host
                timeout = atoi(optarg);
                break;
            }

            case 'u':
            {
                uflag =1;
                //listening port on bootstrap host
                uuid = atoi(optarg);
                break;
            }

            case 'l':
            {
                lflag =1;
                bzero(log, 32);
                strncpy(log, optarg, 32);
                break;
            }

            default:
                fprintf(stderr, 
                 "Usage:%s -b <bootstrap ip> -p <bootstrap port> -u <uuid>\n",
                    argv[0]);
                exit(EXIT_FAILURE);
            }
        }

    //check required arguments
    if(!bflag || !pflag || !uflag) {
        fprintf(stderr, 
         "Usage:%s -b <bootstrap ip> -p <bootstrap port> -u <uuid> -l <logfile>\n",
            argv[0]);
        exit(EXIT_FAILURE);
    }

    if(!tflag) timeout = 5;
    if(!lflag) strcpy(log, "gossip.log");
        
    strcpy(cfg.logFile, log);
        
    struct addrinfo hints, *result, *rp;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol =0;

    int s = getaddrinfo(ip, port, &hints, &result);
    if (s != 0) {
        printf("getaddrinfo error:%s", gai_strerror(s));
        //exit(EXIT_FAILURE);
        //return;
    }

    for(rp = result; rp != NULL; rp = result->ai_next) {
        struct sockaddr_in *bootstrap = (struct sockaddr_in *)rp->ai_addr; 
        cfg.ip = ntohl(bootstrap->sin_addr.s_addr);
        cfg.port = ntohs(bootstrap->sin_port);
        break;
    }

    freeaddrinfo(result);
    cfg.timeout = timeout;
    cfg.uuid = uuid;
}

int
main(int argc, char** argv)
{
    MemberCfg cfg;
    
    readInput(argc, argv, cfg);

    boost::shared_ptr<Member> m(new Member(cfg));

    int listenFd = m->getFd(); 
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listenFd, &rfds);

    //run the algo every 'timeout' secs if we don't receive any messages
    struct timespec ts;
    ts.tv_sec = cfg.timeout;
    ts.tv_nsec = 0;

    while(1) {
        int rv = pselect(listenFd+1, &rfds, NULL, NULL, &ts, NULL);
        if(rv == -1) {
            perror("select()");
        }
        //process received message, detect failures and disseminate
        //membership information
        m->run();
    }
    return 0;
}
