#pragma once
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef _WIN32 //windows headers 
    #include <winSock2.h>
    #include <wS2tcpip.h>
    typedef SOCKET socket_t;
    #define INVALID_SOCK INVALID_SOCKET
    #define CLOSE_SOCKET(s) closesocket(s)
    #ifndef SHUT_WR
        #define SHUT_WR SD_SEND
    #endif
    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif
#else //linux headers 
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    typedef int socket_t;
    #define INVALID_SOCK (-1)
    #define CLOSE_SOCKET(s) close(s)
#endif

using namespace std;

const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int IO_BUFFER_SIZE = 64 * 1024; 
const int CLIENT_TIMEOUT_SEC = 30;
const int TRAILER_SIZE = 4;
const char STATUS_OK = 0;
const char STATUS_ERROR = 1;

// Helper.h should contain the structs, functions, constants, etc. that are used more than one time in the project. For example, the structs that are used in both server and client, the constants that are used in all three files. 
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};

struct TTDBHeader
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};

struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};



inline  bool netInit(){
    #ifdef _WIN32
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2,2),&wsa) == 0;
    #else
        return true;

    #endif
}

inline void netCleanup(){
    #ifdef _WIN32
        WSACleanup();
    #endif

}

inline socket_t makeTcpSocket(){
    soct_t s = socekt(AF_INET, SOCK_STREAM,0);
    #ifdef _WIN32
        if( s != INVALID_SOCK){
            SetHandleInformation((HANDLE)s,HANDLE_FLAG_INHERIT,0);
        }
    #else
        if (s != INVALID_SOCK){
            fcntl(s,F_SETFD,FD_CLOEXEC);

        }
    #endif
    return s;
}


inline bool setRecvTimeout(socket_t s, int seconds){
    #ifdef _WIN32
        DWORD ms = (DWORD)seconds * 1000;
        return setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char *)&ms,sizeof(ms))==0;
    
    #else
        timeval tv;
        tv.tv_sec = seconds;
        tv.tv_usec =0;
        return setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)) == 0;
    #endif
}


