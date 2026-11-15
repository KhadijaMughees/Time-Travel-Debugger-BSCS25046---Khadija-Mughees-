// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <signal.h>
#include <vector>
#include <sstream>
#include <cctype>

#include "helper.h"
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";
   // fixed buffer for streaming files to and from the socket
const char *TDBG_FILE = "ttdb_last.tdbg"; // where the downloaded .tdbg is saved
const int MAX_JUMP = 1000000000;          // biggest n accepted by jmp
string LB_HOST = "127.0.0.1";
int LB_PORT = 5000;

const char *TDBG_FILE_B = "ttdb_last_b.tdbg";
bool useSecondFile = false;
const int MAX_NAME_LENGTH =  1000000;

// These two must be the same as in Phase 1




// ---- Structs (same as Phase 1, so the file can be read back exactly as it was written) ----

// ---- Structs only the client needs ----
struct TdbgReader
{
    FILE *file;
    TTDBHeader header;
    vector<int64_t> index;
};
struct DebugSession
{
    int active_flag;     // false until a "start" has worked
    TdbgReader reader;   // the .tdbg file stays open for the whole session
    int64_t currentStep; 
};

// =============================================================================================
// PART A: TALKING TO THE LOAD BALANCER
// =============================================================================================




bool uploadSource(int sock, const char *sourcePath)
{
    // have to do checksum and then send
    // When the file is finished, call shutdown(sock, SHUT_WR). This is how the server knows the string is complete.
    // Return false if the file cannot be opened or any send fails.

    FILE *f = fopen(sourcePath,"rb");
    if(f == nullptr){
        return false;
    }

    Crc32 crc;
    char status = STATUS_OK;


    if(!sendWithCrc(sock,&status,1,crc)){
        fclose(f);
        return false;
    }



    char buffer[IO_BUFFER_SIZE];
    while(true){
        size_t n = fread(buffer,1,IO_BUFFER_SIZE,f);
        if(n==0){
            break;
        }
        if(!sendWithCrc(sock,buffer,n,crc)){
            fclose(f);
            return false;
        }
    }
    fclose(f);

    if(!sendTrailer(sock,crc)){
        return false;
    }
    shutdown(sock,SHUT_WR);
    return true;
}
bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    // Reply on the wire: [status][payload][CRC-32]. The CRC covers the status byte too.
    // Feed every received byte (status included) to a TrailerChecker.
    // Status 1: collect the payload into outError. Status 0: write the payload to the file.
    // At the end call verify(). If it fails, delete the file, set outError to
    // "checksum mismatch, response corrupted" and return false.


    setRecvTimeout(sock,CLIENT_TIMEOUT_SEC);

    TrailerChecker checker;
    string released;
    char buffer[IO_BUFFER_SIZE];
    bool gotStatus = false;
    char status = STATUS_OK;
    FILE *f = nullptr;
    outError = "";


    while(true){
        int n = recv(sock,buffer,IO_BUFFER_SIZE,0);

        if(n == 0){
            break;
        }

        if(n<0){
            if(f != nullptr){
                fclose(f);
                remove(outTdbgPath);
            }
            outError = "connection error while receiving reply";
            return false;
        }

        checker.feed(buffer,n,released);
        size_t start = 0;
        if(!gotStatus && !released.empty()){
            status = released[0];
            gotStatus = true;
            start = 1;

            if (status == STATUS_OK){
            f= fopen(outTdbgPath,"wb");
            if(f==nullptr){
                outError = "client could not create the .tdbg file";
                return false;
            }
          }
        }

        

         if(released.size() > start){
        if(status == STATUS_OK){
            fwrite(released.data() + start,1,released.size()-start,f);
        }
        else{
            outError.append(released,start,string::npos);
        }
    }
    }

    if(f != nullptr){
        fclose(f);
    }


   if(!gotStatus){
    outError = "connection closed with no reply";
    return false;
   }

   if(!checker.verify()){
    if(status == STATUS_OK){
        remove(outTdbgPath);
    }
    outError = "checksum mismatch, response corrupted";
    return false;
   }

   if( status != STATUS_OK){
        return false;
   }

   return true;
   
}

// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================
void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
    r.index.clear();
    r.index.shrink_to_fit();
    if(r.file != nullptr){
        fclose(r.file);
        r.file = nullptr;
    }
}
bool openTdbg(const char *path, TdbgReader &r)
{
    r.file = fopen(path,"rb");
    if(r.file == nullptr){
        return false;
    }

    TTDBHeader &h = r.header;
     bool ok = fread(h.magic, 1, 4, r.file) == 4
           && fread(&h.version, sizeof(int32_t), 1, r.file) == 1
           && fread(&h.stepCount, sizeof(int32_t), 1, r.file) == 1
           && fread(&h.indexOffset, sizeof(int64_t), 1, r.file) == 1;

    if(ok){
        ok = memcmp(h.magic,"TTDB",4) == 0 && h.stepCount >0;
    }


    if(ok){
        fseek(r.file,0,SEEK_END);
        int64_t filesize = ftell(r.file);
        int64_t expectedend = h.indexOffset + (int64_t)h.stepCount*8;
        ok = h.indexOffset >= 20 && expectedend == filesize;
    }
    if(ok){
        r.index.resize(h.stepCount);
        ok = fseek(r.file, (long)h.indexOffset, SEEK_SET) == 0
          && fread(r.index.data(), sizeof(int64_t), h.stepCount, r.file) == (size_t)h.stepCount;
    }



    if(!ok){
        closeTdbg(r);
        return false;
    }
    return true;
}

bool readInt32(FILE *f, int32_t &value){
    return fread(&value, sizeof(int32_t),1,f) ==1;

}

bool readstr(FILE *f, string &out){
    int32_t size =0;
    if(!readInt32(f,size)){
        return false;
    }

    if(size <0 || size > MAX_NAME_LENGTH){
        return false;
    }
    out.resize(size);
    if(size > 0 && fread(&out[0],1,size,f) != (size_t)size){
        return false;
    }
    return true;
}


bool readvar(FILE *f, Variable &v){
    return readstr(f,v.name) && readInt32(f,v.value);
}


bool readframe(FILE *f, Frame &fr){
    if(!readstr(f,fr.func_name)){
        return false;
    }
    if(!readInt32(f,fr.argc)){
        return false;
    }
    if(fr.argc < 0 || fr.argc > MAX_VARS_PER_FRAME){
        return false;
    }

    for(int j=0;j<fr.argc;j++){
        if(!readvar(f,fr.argv[j])){
            return false;
        }
    }

    if(!readInt32(f,fr.returnLine)){
        return false;
    }

    if(!readInt32(f,fr.localCount)){
        return false;
    }

    if(fr.localCount<0||fr.localCount>MAX_VARS_PER_FRAME){
        return false;
    }

    for(int k=0;k<fr.localCount;k++){
        if(!readvar(f,fr.locals[k])){
            return false;
        }
    }

    return true;
}

Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{

    if(r.file == nullptr || step <0 || step >= r.header.stepCount){
        return nullptr;
    }

    if(fseek(r.file,(long)r.index[step],SEEK_SET) != 0){
        return nullptr;
    }

    Snapshot *snap = new Snapshot();
    bool ok = readInt32(r.file, snap->stackDepth)
           && snap->stackDepth >= 0
           && snap->stackDepth <= MAX_STACK_DEPTH;

    for(int i=0;ok && i < snap->stackDepth; i++){
        ok = readframe(r.file,snap->callStack[i]);
    }

    if(!ok){
        delete snap;
        return nullptr;
    }

    return snap;
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
   s.active_flag =0;
   s.currentStep =0;
   s.reader.file = nullptr;
   memset(&s.reader.header,0,sizeof(TTDBHeader));
   s.reader.index.clear();
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.

    if(s.active_flag){
        closeTdbg(s.reader);
        s.active_flag =0;
    }
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
    Snapshot *snap = readSnapshotAt(s.reader,(int32_t)s.currentStep);
    if(snap == nullptr){
        cout<<"damaged file"<<endl;
        return false;
    }
    printSnapshot((int32_t)s.currentStep+1,s.reader.header.stepCount,*snap);
    delete snap;
    return true;
}
bool moveBy(DebugSession &s, int32_t delta)
{
    if(!s.active_flag){
        cout<<"no session yet"<<endl;
        return false;
    }

    int64_t target = s.currentStep + delta;
    if(target < 0 || target>=s.reader.header.stepCount){
        cout<<"cannot move to that step"<<endl;
        return false;
    }
    s.currentStep = target;
    return showCurrent(s);
}
bool cmdStart(DebugSession &s, const string &sourcePath)
{
    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).
    FILE *test = fopen(sourcePath.c_str(),"rb");
    if(test == nullptr){
        cout<<"cannot open "<<sourcePath<<endl;
        return false;
    }
    fclose(test);
    const char *newpath;
    if(useSecondFile == true){
        newpath = TDBG_FILE_B;
    }
    else{
        newpath = TDBG_FILE;
    }

    int sock = connectToHost(LB_HOST.c_str(),LB_PORT);
    if(sock < 0){
        cout<<"could not connect to load balancer"<<endl;
        return false;
    }
    if(!uploadSource(sock,sourcePath.c_str())){
        cout<<"could not upload "<<sourcePath<<endl;
        CLOSE_SOCKET(sock);
        return false;
    }

    string error;
    bool downloaded = receiveResponse(sock,newpath,error);
    CLOSE_SOCKET(sock);

    if(!downloaded){
        cout<<"Err: "<<error<<endl;
        return false;
    }



    TdbgReader newReader;
    newReader.file = nullptr;
    if(!openTdbg(newpath, newReader)){
        cout<<"server sent a damaged .tdbg file"<<endl;
        remove(newpath);
        return false;
    }

     Snapshot *probe = readSnapshotAt(newReader,0);
    if(probe == nullptr){
        cout<<"server sent a damaged .tdbg file"<<endl;
        closeTdbg(newReader);          // close BEFORE remove (Windows cannot delete an open file)
        remove(newpath);
        return false;
    }
    delete probe;
    
    if(s.active_flag){
        closeTdbg(s.reader);
    }
    s.reader = newReader;
    s.currentStep =0;
    s.active_flag =1;
    useSecondFile = !useSecondFile;

    return showCurrent(s);
}
bool cmdNext(DebugSession &s) {
    return moveBy(s,1);
}
bool cmdPrv(DebugSession &s) {
    return moveBy(s,-1);
}
bool cmdJmpNext(DebugSession &s, int32_t n) {
    return moveBy(s,n);
}
bool cmdJmpPrv(DebugSession &s, int32_t n) {
     return moveBy(s,-n);
}

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
    cout<<"Total Steps: "<<totalSteps<<endl;
    cout<<"Current Step: "<<step<<endl;
    cout<<"Call Stack : "<<endl;
    for(int i=0;i<s.stackDepth;i++){
        const Frame &fr = s.callStack[i];
        cout<<" ["<<i<<"] "<<fr.func_name<<endl;
        cout << "      args:   ";
        if (fr.argc == 0) cout << "(none)";
        for (int j = 0; j < fr.argc; j++)
        {
            cout << fr.argv[j].name << "=" << fr.argv[j].value << "  ";
        }
        cout << endl;

        cout << "      locals: ";
        if (fr.localCount == 0) cout << "(none)";
        for (int k = 0; k < fr.localCount; k++)
        {
            cout << fr.locals[k].name << "=" << fr.locals[k].value << "  ";
        }
        cout << endl;
    }
}
void printHelp()
{
    // Print the commands
    cout << "Commands:" << endl;
    cout << "  start <file>      send a program to the server and begin debugging" << endl;
    cout << "  next              go one step forward" << endl;
    cout << "  prv               go one step back" << endl;
    cout << "  jmp next <n>      jump n steps forward" << endl;
    cout << "  jmp prv <n>       jump n steps back" << endl;
    cout << "  help              show this list" << endl;
    cout << "  quit | exit       leave the debugger" << endl;
}

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================


bool parseNum(const string &text, int32_t &out){
    if(text.empty() || text.size() > 10){
        return false;
    }


    for(char ch : text){
        if(!isdigit((unsigned char)ch)){
            return false;
        }
    }
    long long val = stoll(text);
    if(val < 1 || val > MAX_JUMP){
        return false;
    }

    out =(int32_t)val;
    return true;




}



bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.

    vector<string> words;
    istringstream input(line);
    string word;
    while(input >> word){
        words.push_back(word);
    }

    if(!words.empty() && words[0] == PROGRAM_NAME){
        words.erase(words.begin());
    }

    if(words.empty()){
        return true;
    }

    const string &cmd = words[0];
    if(cmd == "start" && words.size() == 2){
        return cmdStart(s,words[1]);
    }
    if(cmd == "next"  && words.size() == 1){
        return cmdNext(s);
    }
    if(cmd == "prv" && words.size() == 1){
        return cmdPrv(s);
    }
    if(cmd == "jmp" && words.size() == 3){
        int32_t n =0;
        if(parseNum(words[2],n)){
            if(words[1] == "next"){
                return cmdJmpNext(s,n);

            }
            if(words[1] =="prv"){
                return cmdJmpPrv(s,n);
            }
        }
    }

    if (cmd == "help" && words.size() == 1){
        printHelp();
        return true;
    }
    if((cmd == "quit" || cmd=="exit") && words.size() == 1){
        quitRequested = true;
        return true;
    }

    cout<<"unknown command"<<endl;
    printHelp();
    return false;
}
int main(int argc, char *argv[])
{
    // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function. 

    // DebugSession session; initSession(session);
    // Print a short welcome and the help text.
    //
    // Infinite loop:
    //   if quitRequested, break
    //
    // After the loop: closeSession(session) and return 0.
    // Never exit the loop because of an error. Only quit or the end of the input leaves it.
    if(argc != 3){
        cout<<"usage: ./client <lb_ip> <lb_port>"<<endl;
        return 1;
    }
    LB_HOST = argv[1];
    LB_PORT = atoi(argv[2]);
    if(LB_PORT <1 || LB_PORT > 65535){
        cout<<"invlaid port"<<endl;
        return 1;
    }
    if(!netInit()){
        cout<<"network initialization failed"<<endl;
        return 1;
    }

    DebugSession session;
    initSession(session);
    cout<<"Time Travel Debugger"<<endl;
    printHelp();
    bool quitreq = false;
    string line;
    while(!quitreq){
        cout<<PROMPT<<flush;
        if(!getline(cin,line)){
            break;
        }
        runCommand(session,line,quitreq);
    }
    closeSession(session);
    netCleanup();
    return 0;
}
