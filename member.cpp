#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <arpa/inet.h>
#include <boost/shared_ptr.hpp>
#include "member.h"

const int MSGSIZE = 512;

Member::Member(MemberCfg &cfg):
        _bootstrap(cfg.ip), 
        _port(cfg.port), 
        _timeout(cfg.timeout), 
        _aging(cfg.timeout), 
        _uuid(cfg.uuid),
        _join(false),
        _hb(1)
{
    init(cfg);
}

Member::~Member() {
    close(_fd);
    _logFile.close();
}

int
Member::getFd() 
{
    return _fd;
}

int
Member::init(MemberCfg &cfg) {

    _logFile.open(cfg.logFile);

    _fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(_fd == -1) {
        log("Failed to open UDP socket %d", errno);
        return -1;
    }

    struct sockaddr_in myaddr;
    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr - htonl(INADDR_ANY);
    myaddr.sin_port = htons(_port);

    socklen_t addrlen = sizeof(struct sockaddr_in);

    int ret = bind(_fd, (struct sockaddr *)&myaddr, addrlen);
    if(ret == -1) {
        log("Failed to bind UDP socket %d", errno);
        return -1;
    }

    //set recv timeout
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if(setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        log("Failed to set socket rcvto %d ", errno);
        return -1;
    }
}

int
Member::run() 
{
    processMessages();             
    Timestamp now = Time::now();
    ElapsedTime e = now - _joinReqTstamp;

    //don't send join request more than once every 5 secs
    if(!_join && e.count() > 5) {
        return sendJoinReq();
    }

    //
    //detect failures
    //
    //goosip member ship list

    return 0;
}

int
Member::sendJoinReq()
{
    log("sending join request %u", _hb);

    //send join request to introducer
    JoinReq msg;
    msg.hdr.type = htons(JOIN_REQ);
    msg.uuid = htonl(_uuid);
    msg.hb = htonl(_hb++); //increment for every message

    struct sockaddr_in introducer;
    memset((char *)&introducer, 0, sizeof(introducer));
    introducer.sin_family = AF_INET;
    introducer.sin_addr.s_addr = htonl(_bootstrap);
    introducer.sin_port = htons(_port);
    _joinReqTstamp = Time::now();
    
    socklen_t addrlen = sizeof(introducer);
    
    return sendto(_fd, &msg, sizeof(JoinReq), 0, 
                    (struct sockaddr *)&introducer, addrlen);
}

int
Member::processMemberList(
    unsigned char *buf,
    size_t len,
    struct sockaddr_in *remote
    ) 
{
    
    MbrListHdr *msg= (MbrListHdr *)buf;
    int nMembers = ntohl(msg->nMembers);
    buf += sizeof(MbrListHdr);

    for(int m = 0; m < nMembers; ++m)
    {
        MemberRecord *rec = (MemberRecord *)buf;
        uuid_t uuid = ntohl(rec->uuid);
        //skipping our own record
        if(uuid == _uuid) continue;

        uint32_t ip, hb = ntohl(rec->hb);
        uint16_t port;
        MembersIter iter = _members.find(uuid);
        if(iter == _members.end())
        {
            //sender doesn't add his own ip/port
            //it is upto receiver to deduce them 
            if(!rec->ip || !rec->port)
            {
                ip = ntohl(remote->sin_addr.s_addr);
                port = ntohs(remote->sin_port);
            }
            else {
                ip = ntohl(rec->ip);
                port = ntohs(rec->port);
            }
            log("Adding new member %d hb %d", uuid, hb);
            addMember(ip, port, uuid, hb);
        }
        else {
            //member already exists. update hb if it is greater
            MbrPtr mbr = iter->second;
            if(mbr->hb < hb)
            {
                log("Updating hb %d for uuid %d", hb, uuid);
                mbr->hb = hb;
                mbr->state = ALIVE;
            }
        }
    } 
}

MsgPtr
Member::buildMemberListMsg(size_t &msgSize)
{
    msgSize = sizeof(MsgHdr) + (_members.size()+1)*sizeof(MemberRecord);

    MsgPtr ret(new unsigned char [msgSize]); 
    unsigned char *buf = ret.get();

    MbrListHdr *hdr= (MbrListHdr *)buf;
    hdr->hdr.type = htons(MEMBER_LIST);
    hdr->nMembers = htonl(_members.size()+1);
    buf += sizeof(MbrListHdr);

    MemberRecord *msg = (MemberRecord *)buf; 
    for(MembersIter iter = _members.begin();
        iter != _members.end();
        ++iter)
    {
        MbrPtr mbr(iter->second);
        msg->hb = htonl(mbr->hb);
        msg->uuid = htonl(mbr->uuid);
        msg->ip = htonl(mbr->ip);
        msg->port = htons(mbr->port);
        buf += sizeof(MemberRecord);
        msg = (MemberRecord *)buf; 
    }

    //add ourselves to member list at the end
    msg->hb = htonl(_hb++);
    msg->uuid = htonl(_uuid);
    //The receipient can figure out our ip/port
    //from socket recv call
    msg->ip = 0; 
    msg->port = 0;

    return ret;
}

int
Member::sendMemberList(struct sockaddr_in *remote)
{
    size_t len;
    MsgPtr msg = buildMemberListMsg(len);

    return sendto(_fd, msg.get(), len, 0,
                    (struct sockaddr *)remote, 
                    sizeof(struct sockaddr_in));
}

int
Member::sendJoinResp(
    struct sockaddr_in *remote,
    JoinReq *req)
{
    socklen_t addrlen = sizeof(struct sockaddr_in);
    JoinResp resp;

    resp.hdr.type = htons(JOIN_RESP);
    resp.hdr.status = htons(OK);
    //
    //send join response
    return sendto(_fd, &resp, sizeof(resp), 0,
            (struct sockaddr *)remote, 
            sizeof(struct sockaddr_in));
}

//check if member exists before calling addMember
int
Member::addMember(uint32_t ip, uint16_t port, uuid_t uuid, uint32_t hb)
{
    //add this host to member cluster
    MbrPtr mbr(new MemberEntry);
    mbr->ip = ip;
    mbr->port = port;
    mbr->uuid = uuid;
    mbr->tstamp = Time::now();
    mbr->hb = hb;
    mbr->state = ALIVE;

    _members.insert(std::pair<uuid_t, MbrPtr >(uuid, mbr));
    return 0;
}

int
Member::processMessages()
{
    struct sockaddr_in remote;
    ssize_t recvLen;
    unsigned char buf[MSGSIZE];
    socklen_t addrlen = sizeof(remote);
    //process messages
    //
    recvLen = recvfrom(_fd, buf, MSGSIZE, 0, (struct sockaddr *)&remote, &addrlen);
    //timed out or error
    if(recvLen == -1) return -1;

    char rip[INET_ADDRSTRLEN];
    bzero(rip, INET_ADDRSTRLEN);

    inet_ntop(AF_INET, &(remote.sin_addr), rip, INET_ADDRSTRLEN);
  
    if(recvLen > 0) {
        MsgHdr *hdr= (MsgHdr *)buf;
        switch(ntohs(hdr->type)) {
            case JOIN_REQ:
            {
                JoinReq *req = (JoinReq *)buf;
                log("Got join msg from %s port %d uuid %u hb %u",
                        rip,
                        ntohs(remote.sin_port),
                        ntohl(req->uuid),
                        ntohl(req->hb));
                addMember(ntohl(remote.sin_addr.s_addr),
                          ntohs(remote.sin_port),
                          ntohl(req->uuid),
                          ntohl(req->hb));
                sendJoinResp(&remote, req);
                break;
            }

            case JOIN_RESP:
            {
                JoinResp *resp= (JoinResp *)buf;
                log("Got join resp msg from %s port %d status %d",
                        rip,
                        ntohs(remote.sin_port),
                        ntohs(resp->hdr.status));
                if(ntohs(resp->hdr.status) == OK) {
                    log("Joining cluster");
                    _join = true;
                }
                else {
                    log("join failed %d", ntohs(resp->hdr.status));
                }
                break;
            }

            //MEBER_LIST msg also doubles as JOIN_RESP
            case MEMBER_LIST:
            {
                MbrListHdr *msg = (MbrListHdr *)buf;
                log("Got mbr list from %s port %d numMembers %d",
                        rip,
                        ntohs(remote.sin_port),
                        ntohl(msg->nMembers));
                if(_join) processMemberList(buf, recvLen, &remote);
                break;
            }

            default:
            {
                log("unknown msg type %d from %s:%d", 
                    ntohs(hdr->type), 
                    rip,
                    ntohs(remote.sin_port));
                break;
            }
        }
    }
}

void
Member::log(const char* format, ...)
{
    char buf[128];
    va_list args;

    va_start(args, format);

    vsnprintf(buf, 64, format, args);
    va_end(args);

    Timestamp t = Time::now();
    std::time_t now = Time::to_time_t(t);
    struct tm *p = localtime(&now);

    char str[256];

    strftime(str, 256, "%a %d %b %Y %T ", p);

    _logFile << str << buf << std::endl;
}

