#include <string>
#include <sys/socket.h>
#include <map>
#include <chrono>
#include <ctime>
#include <fstream>
#include <cstdarg>
#include <fstream>
#include <boost/shared_ptr.hpp>

#ifndef _MEMBER_H
#define _MEMBER_H

typedef struct _member_cfg {
    uint32_t ip;
    int timeout;
    uint32_t uuid;
    uint16_t port;
    char logFile[32];
} MemberCfg;

typedef uint32_t uuid_t;
typedef std::chrono::system_clock Time;
typedef std::chrono::time_point<Time> Timestamp;
typedef std::chrono::duration<double> ElapsedTime;

typedef enum _memberState {
    ALIVE,
    AGING, //hb timed out. we are aging it out
    DEAD
} MemberState;

//message types
const uint16_t JOIN_REQ = 1;
const uint16_t JOIN_RESP = 2;
const uint16_t MEMBER_LIST = 3;

//return codes
const uint16_t OK = 0;
const uint16_t JOIN_FAIL_DUPLICATE_UUID = 1;

//message types and return codes should
//be plain types for easy serialization
typedef struct _msg_hdr {
    uint16_t type;
    uint16_t status;
} MsgHdr;

typedef struct _join_req {
    MsgHdr hdr; //msg hdr
    uint32_t uuid;
    uint32_t hb;
} JoinReq;

typedef struct _join_resp {
    MsgHdr hdr; //msg hdr
} JoinResp;

typedef struct _mbr_list_hdr {
    MsgHdr hdr; //msg hdr
    uint32_t nMembers; //number of members
} MbrListHdr;
//
//this is what we sent to remote hosts in memberList update
typedef struct _member_record{
    uint32_t hb;
    uuid_t uuid;
    uint32_t ip;
    uint16_t port;
} MemberRecord;


//this is what we store internally
typedef struct _member_entry{
    //hearbeast count is set by originating member only
    //however it can be updated from gossip messages from
    //any member as long as the new hb is greater than
    //what we have
    uint64_t hb; 
    //local time when this record was updated.
    //when a record is not updated after _timeout secs
    //then it is marked for deletion.
    //The record is only deleted after a further _aging secs
    //This is to ensure that a member is not added back
    //immediately after deletion. after (_timeout + _aging) secs
    //we can be reasonably confident that the member is down
    Timestamp tstamp;
    uuid_t uuid;
    uint32_t ip;
    uint16_t port;
    MemberState state;
} MemberEntry;


typedef boost::shared_ptr<MemberEntry> MbrPtr;
typedef std::pair<uuid_t, MbrPtr > MbrPair;
typedef std::map<uuid_t, MbrPtr > Members;
typedef Members::iterator MembersIter;
typedef boost::shared_ptr<unsigned char> MsgPtr;

class Member {

    public:
        Member(MemberCfg & cfg);
        ~Member();
        int run();
        int getFd();

    private:
        int init(MemberCfg &cfg);
        void log(const char* format, ...);
        int processMessages();
        int sendJoinReq();
        int sendJoinResp(struct sockaddr_in *remote, JoinReq *msg);

        MsgPtr buildMemberListMsg(size_t &msgSize);

        int sendMemberList(struct sockaddr_in *remote);

        int
        processMemberList(
            unsigned char *buf,
            size_t len,
            struct sockaddr_in *remote
            ); 

        int
        addMember(uint32_t ip, uint16_t port, uuid_t uuid, uint32_t hb);

        uint32_t _bootstrap; //boostrap ip for cluster
        uuid_t _uuid; //unique identifier for this member
        uint32_t _hb; //local heartbeat
        uint16_t _port; //listening port
        uint16_t _timeout; //mark member record as deleted after timeout
        uint16_t _aging; //delete member record after aging
        int _fd; //socket file descriptor for sending/receiving messages
        std::ofstream _logFile; //for logging events
        Timestamp _joinReqTstamp; //keep track of last join req
        bool _join; //whether this member was accepted into cluster
        Members _members;
};

#endif
