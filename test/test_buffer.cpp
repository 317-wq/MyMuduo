#include "../include/Buffer.h"
#include <iostream>
using std::cout;
using std::endl;

int main(){
    Buffer buffer;
    // cout << "readpos" << endl;
    // cout << buffer.ReadPos() << endl;

    // cout << "writepos" << endl;
    // cout << buffer.WritePos() << endl;

    // cout << "buffersize" << endl;
    // cout << buffer.BufferSize();

    // cout << "" << endl;
    // cout << buffer.ReadableSize() << endl;

    // cout << buffer.FrontRemainSize() << endl;

    // cout << buffer.AfterRemainSize() << endl;

    // cout << buffer.RemainSize() << endl;
    std::string str;

    for(int i = 1; i <= 1025; ++i)
        str += "a";
    buffer.WriteStringData(str);

    cout << buffer.GetReadPos() << endl;
    cout << buffer.GetWritePos() << endl;

    cout << buffer.ReadableSize() << endl;
    cout << buffer.AfterRemainSize() << endl;
}