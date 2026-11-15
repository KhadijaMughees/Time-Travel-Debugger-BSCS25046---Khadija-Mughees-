// ======================= TIME-TRAVEL DEBUGGER - LOAD BALANCER TEMPLATE =======================

// What load balancer does:
//   1. Many clients can connect at the same time. Each one sends one string (the source code). 
//   2. Clients wait in a queue (first come, first served).
//   3. For each client, in order: make sure the server is running (start it with system() if not),
//      pass the string to the server, pass the server's reply back to the client.
//
// It does not read or understand the messages. It only moves bytes.
// The only messages it writes itself are errors ("server busy", "server failed to start").
//
// Two threads:
//   acceptor thread (acceptLoop) -> takes new clients and puts them in the queue. It never waits for the server.
//   main thread (dispatcherLoop) -> takes ONE client out of the queue and serves it completely, then the next.
//
// How the server is started:
//   system("./server 5001 &");
//   The "&" is required. Without it, system() waits until the server exits and the load balancer freezes.
//
//
// What the server program must do (the load balancer depends on this):
//   - it takes its port as the first argument, for example: ./server 5001
//   - it reads the string until the connection's write side is closed (recv returns 0)
//   - it sends its reply as [status: 1 byte][payload], then closes the connection 


#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <mutex>



#include <signal.h>

#include "helper.h"
#include <ctime>

using namespace std;


// ---- Constants ----
#ifdef _WIN32
const char *SERVER_PATH = "server.exe";
#else
const char *SERVER_PATH = "./server";
#endif   // the program we start when the server is down
    // fixed buffer for moving bytes
const int LISTEN_BACKLOG = 16;             // connections the OS holds until accept() takes them
const int MAX_PENDING_CLIENTS = 8;         // clients allowed to wait in our queue. More than this get "server busy".
const int STARTUP_MAX_ATTEMPTS = 50;       // after starting the server, how many times we try to connect
const int STARTUP_POLL_INTERVAL_MS = 100;  // wait between those tries (50 x 100 ms = 5 seconds in total)
int lbPort = 5000;
string serverHost = "127.0.0.1";
int serverPort = 5001;
const int UPLOAD_TIMEOUT_SEC = 10;

const int ACCPET_POLL_MS = 500;
const int DISPATCH_NAP_MS = 50;




// ---- Queue: here it holds client sockets (int). ----
// Implement it yourself, do not use std::queue.
template <typename T>
class Queue
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *front, *rear;
    int count;


public:
    // Implement these functions:
    Queue()
    {
        front = rear = nullptr;
        count =0;

    }
    ~Queue()
    {
        while(!isEmpty()){
            dequeue();
        }
    }
    void enqueue(const T &val)
    {
        Node* n = new Node;
        n->data=val;
        n->next = nullptr;

        if(rear == nullptr){
            front = rear = n;
            
        }
        else{
            rear->next = n;
            rear = n;
        }
        count++;
    }
    T dequeue()
    {
        if(front == nullptr){
            return T();
        }
        Node* temp = front;
        T val = temp->data;
        front = front->next;
        if(front == nullptr){
            rear = nullptr;
        }
        delete temp;
        count--;
        return val;


    }
    bool isEmpty()
    {
        return front == nullptr;
    }
    int size()
    {
        return count;
    }
};

struct ClientInfo
{
    int sock;           // the connected socket
    string address;     // the client's IP as text, e.g. "127.0.0.1"
    int port;           // the client's port
    time_t arrivedAt;   // when accept() returned
};

// The queue is used by two threads, so every use of it must be inside queueMutex.
Queue<ClientInfo> pendingClients;
mutex queueMutex;
volatile sig_atomic_t flag = 1;


// ---- Basic socket helpers ----

bool sendError(int sock, const string &message)
{
    // Reply format: [status: 1 byte][payload until close][checksum: 4 bytes].
    // For an error: send one byte with value 1, then the message text. 

    Crc32 crc;
    char status = STATUS_ERROR;

    if(!sendWithCrc(sock,&status,1,crc)){
        return false;
    }

    if(!sendWithCrc(sock,message.data(),message.size(),crc)){
        return false;
    }

    return sendTrailer(sock,crc);
}

void refuseClient(int sock, const string &message){
    sendError(sock,message);
    shutdown(sock,SHUT_WR);
    char junk[1024];
    while(waitForReadable(sock,0) && recv(sock,junk,sizeof(junk) ,0) > 0){

    }

    CLOSE_SOCKET(sock);
}
int createListenSocket(int port)
{ 
    // Create a TCP socket with SOCK_CLOEXEC(flag).
    // set SO_REUSEADDR so we can restart the load balancer right away.
    // bind() to the port on any address, then listen(LISTEN_BACKLOG).
    // If bind or listen fails, print the error and exit, because without this socket we cannot work.


    socket_t sock = makeTcpSocket();
    if(sock == INVALID_SOCK){
        cout<<"could not create listen socket"<<endl;
        exit(1);
    }

    int yes = 1;
    setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(const char *)&yes, sizeof(yes));

    sockaddr_in address;
    memset(&address,0,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    if(bind(sock,(sockaddr *)&address,sizeof(address)) < 0){
        cout<<"bind failed :("<<endl;
        exit(1);
    }

    if(listen(sock,LISTEN_BACKLOG)<0){
        cout<<"listen failed :("<<endl;
        exit(1);
    }

    return sock;
}
int connectToServer(const char *host, int port)
{
    // Create a TCP socket (SOCK_CLOEXEC) and connect it to host:port.
    // Return the socket, or -1 if the connection fails 
    return connectToHost(host,port);
}

// =============================================================================================
// PART 1: WAKING UP THE SERVER IF IT IS DOWN
// =============================================================================================


bool startServer(int port)
{
    // Build the command text: SERVER_PATH + " " + port as text + "&"
    //   Example: "./server 5001 &"
    // Call system() with it.
    // Return true if system() returned 0, otherwise false.
    #ifdef _WIN32
        string command = string("start /B ") + SERVER_PATH+" "+to_string(port);
    #else
        string command = string(SERVER_PATH) +" "+ to_string(port) + " &";
    #endif
    int res = system(command.c_str());
    return res == 0;
}
int connectToServerWithWakeup()
{
    // 1. Try connectToServer(). If it works, return that socket. The server is alive.
    // 2. Otherwise call startServer(). If it fails, return -1.
    // 3. Try connectToServer() up to STARTUP_MAX_ATTEMPTS times 
    // 4. Return the first working socket, or -1 if the server never came up.

    int sock = connectToServer(serverHost.c_str(),serverPort);
    if(sock != -1){
        return sock;
    }


    if(!startServer(serverPort)){
        return -1;
    }


    for(int attempt =0; attempt < STARTUP_MAX_ATTEMPTS; attempt++){
        sleepMs(STARTUP_POLL_INTERVAL_MS);
        sock = connectToServer(serverHost.c_str(),serverPort);
        if(sock != -1){
            return sock;
        }

    }

    return -1;
}


// =============================================================================================
// PART 2: PASSING THE STRING AND THE REPLY (one client, called by the main thread only)
// =============================================================================================


void relayOneClient(ClientInfo &c)
{
    // 1. serverSock = connectToServerWithWakeup().
    //    If it is -1: sendError "backend server failed to start", close clientSock, return.
    // 3. Client -> server: repeat recv() from clientSock into a buffer of IO_BUFFER_SIZE and sendAll() it to serverSock.
    //    Stop when recv() returns 0 (the client finished sending) or fails.
    // 4. Call shutdown(serverSock, SHUT_WR), so the server knows the string is complete.
    // 5. Server -> client: repeat recv() from serverSock and sendAll() to clientSock.
    //    Stop when recv() returns 0 (the server sent its whole reply and closed) or fails.
    // 6. Close both sockets.
    int clientSock = c.sock;
    int serverSock = connectToServerWithWakeup();
    if(serverSock == -1){
        refuseClient(clientSock, "backend server failed to start");
        return;
    }

    setRecvTimeout(clientSock, UPLOAD_TIMEOUT_SEC);
    setRecvTimeout(serverSock,CLIENT_TIMEOUT_SEC);

    

    char buffer[IO_BUFFER_SIZE];
    string released;

    TrailerChecker uploadChecker;
    bool uploadok = true;
    bool serverok = true;

    while(true){
        int n= recv(clientSock,buffer,IO_BUFFER_SIZE,0);
        if(n==0){
            break;
        }

        if( n < 0){
            uploadok = false;
           break;
        }

        uploadChecker.feed(buffer,n,released);
        if(!released.empty()){
            if(!sendAll(serverSock,released.data(),released.size())){
                serverok = false;
                break;
            }
        }
    }

    if(!serverok){
        CLOSE_SOCKET(serverSock);
        refuseClient(clientSock,"lost connection to the backend server");
        return;
    }


    if(!uploadok || !uploadChecker.verify()){
        CLOSE_SOCKET(serverSock);
        if(!uploadok){
            refuseClient(clientSock,"upload failed, connection error or timeout");
        }
        else{
            refuseClient(clientSock,"upload checksum mismatch");
        }
        return;
    }


    sendAll(serverSock,uploadChecker.trailer().data(),TRAILER_SIZE);
    shutdown(serverSock,SHUT_WR);

    TrailerChecker replycheck;
    bool replyok = true;
    while(true){

        int n= recv(serverSock,buffer,IO_BUFFER_SIZE,0);
        if(n==0){
            break;
        }
        if(n<0){
            replyok= false;
            break;
        }

        replycheck.feed(buffer,n,released);
        if(!released.empty()){
            if(!sendAll(clientSock,released.data(),released.size())){
                replyok = false;
                break;
            }
        }

    }


    if(replyok && replycheck.verify()){
        sendAll(clientSock,replycheck.trailer().data(),TRAILER_SIZE);
    }


    CLOSE_SOCKET(serverSock);
    CLOSE_SOCKET(clientSock);
}


// =============================================================================================
// PART 3: THE TWO THREADS
// =============================================================================================


void acceptLoop(int listenSock)
{
    // Repeat: accept() a new client. 
    //  If pendingClients.size() is already MAX_PENDING_CLIENTS: unlock, sendError "server busy, try again later",
    // else
    // Lock queueMutex.
    //   close the client, and go on.
    //   Otherwise enqueue the client socket.
    // Never wait for the server here, so new clients are always accepted quickly.
    // If accept() fails, print the error and keep going (do not end the thread).

    while(flag){
        if(!waitForReadable(listenSock,ACCPET_POLL_MS)){
            continue;
        }

        sockaddr_in addr;
        sock_len_t len = sizeof(addr);
        int clientSock = accept(listenSock, (sockaddr *)&addr, &len);
        if (clientSock < 0){
                cout<<"accept failed, stilll running"<<endl;
                continue;

                
        }

        noInherit(clientSock);

        ClientInfo c;
        c.sock = clientSock;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        c.address = ip;
        c.port = ntohs(addr.sin_port);
        c.arrivedAt = time(nullptr);


        bool accepted = false;
        {
            lock_guard<mutex> lock(queueMutex);
            if(pendingClients.size() < MAX_PENDING_CLIENTS){
                pendingClients.enqueue(c);
                accepted = true;
            }
        }  
        
        if(!accepted){
            refuseClient(clientSock,"server busy, try again later");

        }

    }
}
void dispatcherLoop()
{
    // This runs in the main thread and never ends.
    // Repeat:
    //   Lock queueMutex, take ONE client out of the queue if there is one, unlock.
    //   If we got a client: call relayOneClient(). The next client is taken out of the queue only after this one is finished.

    while(flag){
        ClientInfo c;
        bool gotClient = false;
        {
            lock_guard<mutex> lock(queueMutex);
            if(!pendingClients.isEmpty()){
                c=pendingClients.dequeue();
                gotClient = true;
            }
        }

        if(!gotClient){
            sleepMs(DISPATCH_NAP_MS);
            continue;
        }

        if(time(nullptr) - c.arrivedAt > CLIENT_TIMEOUT_SEC){
            cout<<"dropping "<<c.address<<": "<<c.port<<"(waited too long)"<<endl;
            CLOSE_SOCKET(c.sock);
            continue;
        }
        relayOneClient(c);
    }
}

void handler(int sg) {
    (void)sg;
    flag = 0;
}
int main(int argc, char *argv[])
{   

    if(argc == 3){
        serverHost = argv[1];
        serverPort = atoi(argv[2]);
    }
    signal(SIGINT, handler); // whenever ctrl+c will be pressed to exit the program the flag wll be set so insead of using infinite loop you can loop on flag to safely close things and exit
    // 2. listenSock = createListenSocket(LB_PORT).
    // 3. Start a thread that runs acceptLoop(listenSock) and detach it.
    // 4. Call dispatcherLoop().

    if(!netInit()){
        cout<<"network initialization failed"<<endl;
        return 1;
    }
    int listenSock = createListenSocket(lbPort);
    cout<<"load balancer listening on port"<<lbPort<<endl;
    thread acceptor(acceptLoop,listenSock);
    dispatcherLoop();
    cout<<"shutting down..."<<endl;
    acceptor.join();
    CLOSE_SOCKET(listenSock);
    {
        lock_guard<mutex> lock(queueMutex);
        while(!pendingClients.isEmpty()){
            ClientInfo c = pendingClients.dequeue();
            CLOSE_SOCKET(c.sock);
        }
    }

    netCleanup();
    return 0;
}