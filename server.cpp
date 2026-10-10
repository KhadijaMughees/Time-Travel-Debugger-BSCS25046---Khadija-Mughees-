// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)


#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <stdexcept>
using namespace std;

// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
template <typename T>
class Stack
{
   vector<T> data;


public:
    // Implement these functions:
    Stack()
    { // initialize the stack
    }
    void push(const T &val)
    {

        // pushes the value on the stack if max limit is not reached yet.
        if((int32_t)data.size() >= MAX_STACK_DEPTH){
            return;
        }

        data.push_back(val);
    }
    T pop()
    {
        // pop the top value on the stack
        if(isEmpty()){
            throw std:: underflow_error("Stack is empty");
        }
        T lastval = data.back();
        data.pop_back();
        return lastval;
    }
    T &peek()
    {
        // returns the top value on the stack
        if(isEmpty()){
            throw std::underflow_error("Stack is empty");
        }
        return data.back();
    }
    bool isEmpty()
    {
        return data.empty();
    }
    int32_t depth()
    {
        return (int32_t)data.size();
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        // copies every frame, top to bottom in the array given as a parameter
        // this is what buildSnapshot() call, returns count written

        int32_t count=0;

        for(int i=data.size()-1; i>=0;i--){
            if(count == maxLen){
                break;
            }
            out[count] = data[i];
            count++;
        }

        return count;
    }
};


// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
struct TimelineNode
{
    Snapshot *data;
    TimelineNode *next;
    TimelineNode *prev;
};
class Timeline
{
    TimelineNode *head, *tail;
    int32_t stepCount;

public:
    // Implement these functions
    Timeline()
    {
        head = tail = nullptr;
        stepCount =0;
    }
    void record(Snapshot *s)
    {
        // add record in the timeline
        TimelineNode *n = new TimelineNode;
        n->data = s;
        n->next = nullptr;
        n->prev = tail;

        if(tail == nullptr){
            head = n;
        }
        else{
            tail->next = n;
        }


        tail = n;
        stepCount++;
    }
    TimelineNode *begin()
    {
        return head;
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};

// Core structs
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
void writeHeader(FILE *f, const TTDBHeader &h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);

    // placeholder for other two data members
}

// resolve.bin - bookkeeping
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



// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream &in, string &out)
{
    // reads the next nonblank line
    while(getline(in,out)){
        if(!out.empty() && out.back() =='\r'){
            out.pop_back();
        }

        for(int i=0;i<out.size();i++){
            if(out[i]!=' '){
                return true;
            }
        }
    }

    return false;
}
string firstWord(const string &line)
{
    // returns first word from the input string
    int index =0;
    string first="";
    while(index < line.size() && line[index]==' '){
        index++;
    }

    while(index<line.size() && line[index]!=' '){
        first= first + line[index];
        index++;
    }

    return first;
}
string secondWord(const string &line)
{
    // returns the second word
   
    int index =0;
    string second="";
    while(index < line.size() && line[index]==' '){
        index++;
    }
    while(index<line.size() && line[index]!=' '){
        
        index++;
    }
    while(index < line.size() && line[index]==' '){
        index++;
    }
    while(index<line.size() && line[index]!=' '){
        second = second + line[index];
        index++;
    }
    return second;


}


bool validateProgram(const char *sourcePath)
{
    // for each func defined there should be exactly one func_end and no nested funcs allowed - 

    ifstream read(sourcePath);
    if(!read){
        return false;
    }

    Stack<string> check;
    string line="";
    string word ="";
    while(readSourceLine(read,line)){
        word = firstWord(line);
        

        if(word == "func"){
            if(!check.isEmpty()){
                return false;
            }
            check.push(word);
        }
        else if(word == "func_end"){
            if(check.isEmpty()){
                return false;
            }
            check.pop();
        }
    }

    if(check.isEmpty()){
        return true;
    }
    
    return false;

}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE *f, int64_t offsetField, const string &text)
{
    // writes one [offset(8B)][size(4B)][string] record at the current file position
    // returns this record's own starting byte position

    int64_t position = ftell(f);
    int32_t size = (int32_t)text.size();

    fwrite(&offsetField, sizeof(int64_t),1,f);
    fwrite(&size,sizeof(int32_t),1,f);
    fwrite(text.data(),1,size,f);

    return position;


}
int64_t readResolveRecord(FILE *f, string &outText)
{
    // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.

    int64_t offset = 0;
    int32_t sizeoftext =0;

    if(fread(&offset,sizeof(int64_t),1,f)!=1){
        return -1;
    }

    fread(&sizeoftext,sizeof(int32_t),1,f);

    outText.resize(sizeoftext);
    fread(&outText[0],1,sizeoftext,f);

    return offset;

}
int64_t resolveProgram(const char *sourcePath, const char *resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;
    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount = 0;
    string line ="";
    string word="";
    int64_t pos = 0;
    int64_t patchfunc=-1;
    int64_t mainoffset=-1;
    int64_t nextoffset =0;
    // Every source line becomes one record holding the raw line, as-is.
    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
    // (remember its position) and CALL (remember which function it needs
    // and where its offset field sits).
    // Once the whole file is written, every CALL's offset field is patched
    // with its target's position. Patching happens after the full write
    // Returns the byte offset of main's FUNC header record.
    // if there is no main return the error 

    ifstream read(sourcePath);
    if(!read){
        return -1;
    }

    FILE *write = fopen(resolveBinPath,"wb+");
    if(write == nullptr){
        return -1;
    }

    while(readSourceLine(read,line)){
        word = firstWord(line);
        pos = writeResolveRecord(write,nextoffset,line);
        nextoffset = nextoffset + 8+4+line.size();

        if(word == "func"){
            funcArray[funcCount].funcName = secondWord(line);
            funcArray[funcCount].byteOffsetInResolveBin = pos;
            funcCount++;
        }
        else if(word == "call"){
            patches[patchCount].targetFuncName = secondWord(line);
            patches[patchCount].byteOffsetOfOffsetField = pos;
            patchCount++;
        }

        
        
    }



    for(int i=0;i<patchCount;i++){
        patchfunc = -1;
        for(int j=0;j<funcCount;j++){
            if(funcArray[j].funcName == patches[i].targetFuncName){
                patchfunc = funcArray[j].byteOffsetInResolveBin;
            }
        }

        if(patchfunc == -1){
            fclose(write);
            return -1;
        }

        fseek(write,patches[i].byteOffsetOfOffsetField,0);
        fwrite(&patchfunc,sizeof(int64_t),1,write);


    }


    for(int i=0;i<funcCount;i++){
        if(funcArray[i].funcName == "main"){
            mainoffset = funcArray[i].byteOffsetInResolveBin;
        }
    }


    fclose(write);

    return mainoffset;
}

// PASS 0x2: EXECUTION (tokenization happens here)
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string &line, Token tokens[], int32_t maxTokens)
{
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated


    int32_t ct=0;
    int idx=0;
    string word ="";

    while(idx < line.size() && ct < maxTokens){
        while(idx<line.size() && line[idx]==' '){
            idx++;
        }
        if(idx>=line.size()){
            break;
        }
        word = "";
        while(idx<line.size() && line[idx] != ' '){
            word = word + line[idx];
            idx++;
        }
        tokens[ct].text = word;
        if(ct ==0){
            tokens[ct].type =KEYWORD;


        }
        else if(ct == 1){
             tokens[ct].type =IDENTIFIER;
        }
        else{
             tokens[ct].type =PARAM;
        }

        ct++;
    }

    return ct;
}
Snapshot *buildSnapshot(Stack<Frame> &callStack)
{
    // build the snapshot based on the callStack given
    Snapshot *snap = new Snapshot();
    snap->stackDepth = callStack.snapshot_into(snap->callStack,MAX_STACK_DEPTH);
    return snap;

}


Variable* find_variable(Frame &f, const string &name){
    for(int i=0;i<f.argc;i++){
        if( f.argv[i].name == name){
            return &f.argv[i];
        }
    }

    for(int i=0;i<f.localCount;i++){
        if(f.locals[i].name == name){
            return &f.locals[i];
        }
    }

    return nullptr;
}


void executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
{
    // initialize the call stack
    // make the main frame
    // push main frame on the call stack

    // implementation:
    // execute line by line, and according to the keyword perform action



}

// PASS 0x3: SERIALIZE TIMELINE
void writeTdbg(Timeline &timeline, const char *tdbgPath)
{
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header
}
// main section
int32_t main()
{

    if (!validateProgram("source.bin"))
    {
        // send an error response instead of a .tdbg file
        return 1;
    }

    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");

    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    writeTdbg(timeline, "session.tdbg");

    return 0;
}