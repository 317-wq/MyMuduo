#include "../include/Socket.h"

int main(){

    Socket client;

    InetAddress server(
        "127.0.0.1",
        8888
    );

    client.Connect(server);

    return 0;
}