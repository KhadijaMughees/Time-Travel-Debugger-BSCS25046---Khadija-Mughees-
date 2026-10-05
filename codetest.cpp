   #include <iostream>
   #include <vector>
   #include <cstdint>
   using namespace std;
   const int32_t MAX_STACK_DEPTH = 64;

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
        T lastval = data.back();
        data.pop_back();
        return lastval;
    }
    T &peek()
    {
        // returns the top value on the stack
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
   int main() {
    Stack<int> s;

    cout << "empty? " << s.isEmpty() << " (expect 1)\n";

    s.push(10);
    s.push(20);
    s.push(30);
    cout << "depth: " << s.depth() << " (expect 3)\n";
    cout << "peek: " << s.peek() << " (expect 30)\n";

    s.peek() = 99;
    cout << "peek after edit: " << s.peek() << " (expect 99)\n";

    int arr[10];
    int n = s.snapshot_into(arr, 10);
    cout << "snapshot count: " << n << " (expect 3)\n";
    cout << "snapshot: " << arr[0] << " " << arr[1] << " " << arr[2]
         << " (expect 99 20 10)\n";

    cout << "pop: " << s.pop() << " (expect 99)\n";
    cout << "pop: " << s.pop() << " (expect 20)\n";
    cout << "depth: " << s.depth() << " (expect 1)\n";

    for (int i = 0; i < 100; i++) s.push(i);
    cout << "depth after overfill: " << s.depth() << " (expect 64)\n";
}