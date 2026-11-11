#pragma once
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef _WIN32 //windows headers 
    #include <winsock2.h>
    #include <wS2tcpip.h>
    #include <windows.h>
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
    #include <sys/time.h>
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




//checksum stuff

struct Crc32{
    uint32_t state;

    Crc32(){
        state = 0xFFFFFFFF; 
    }

    void update(const char* data, size_t n){
        for(size_t i =0;i<n;i++){ //process each byte
            state ^= (uint8_t)data[i]; //xor

            for(int k=0;k<8;k++){
                if( state & 1){
                    state = (state >> 1) ^  0xEDB88320; //right shift smth standard polynomial i have given up on this for now

                }
                else{
                    state = state >> 1;
                }
            }
        }
    }

    uint32_t finish()const{
        return ~state;
    }
};


inline void crcToBytes (uint32_t crc,char out[4]){
    for(int i=0;i<4;i++){
        out[i] = (char)((crc >> (8 *i)) & 0xFF);
    }
}

class TrailerChecker{
    Crc32 crc;
    string held;

public:
    void feed(const char* data, size_t n, string& released){
        held.append(data,n);
        released.clear();

        if(held.size() > TRAILER_SIZE){
            size_t safe = held.size() - TRAILER_SIZE;
            released.assign(held,0,safe);
            crc.update(released.data(),released.size());
            held.erase(0,safe);
        }

    }

    bool verify() const {
        if(held.size( ) != TRAILER_SIZE){
            return false;
        }
        char expected[4];
        crcToBytes(crc.finish(),expected);
        return memcmp(expected, held.data(),4) == 0;
    }
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
    socket_t s = socket(AF_INET, SOCK_STREAM,0); //IPv4 TCP
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


//sendall will keep calling send until all requested bytes have been sent
inline bool sendAll(socket_t s, const void*data, size_t len){
    const char* bytes = static_cast<const char*>(data);
    size_t sent = 0;


    while(sent < len){
        int chunk = (int)(len - sent);

        if(chunk > IO_BUFFER_SIZE){
            chunk = IO_BUFFER_SIZE;
        }
       
            
        

        int n= send(s,bytes+sent,chunk,MSG_NOSIGNAL);

        if(n<=0){
            return false;
        }

        sent += n;

    }

    return true;
}



inline bool recvAll(socket_t s, void* data, size_t len){
    char* bytes = static_cast<char*> (data);

    size_t received = 0;
    

    while(received < len){
        int chunk = (int)(len - received);

        if(chunk > IO_BUFFER_SIZE){
            chunk = IO_BUFFER_SIZE;
        }

        int n = recv(s,bytes+received,chunk,0);

        if(n <= 0){
            return false;
        }

        received += n;
    }

    return true;
}


