#pragma once
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef _WIN32 //windows headers 
    #include <WinSock2.h>
    #include <WS2tcpip.h>
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
    #define INVALID_SOCK(-1)
    #define CLOSE_SOCKET(s) close(s)
#endif

using namespace std;

const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int IO_BUFFER_SIZE = 64 * 1024; 
const int CLIENT_TIMEOUT_SEC = 30;
const int TRAILER_SIZE = 4;
const char STATUS_OK = 0;
const char STATUS_ = 1;

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



