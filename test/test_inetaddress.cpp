#include "../include/InetAddress.h"
#include <iostream>

using std::cout;
using std::endl;


int main(){
    InetAddress addr(8080);
    std::string ip = addr.Ip(); cout << "ip: " << ip << endl;
    uint16_t port = addr.Port(); cout << "port: " << port << endl;
    socklen_t len = addr.Length(); cout << "len " << len << endl;
    cout << "addr: " << addr.Addr() << endl;
    return 0;
}