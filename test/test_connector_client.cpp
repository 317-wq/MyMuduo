#include "../include/Connector.h"

#include <iostream>

int main(){

    Connector conn(
        false
    );

    if(
        conn.Connect(
            "127.0.0.1",
            8888
        )
    ){

        std::cout
            << "connect ok"
            << std::endl;

        std::cout
            << conn.Fd()
            << std::endl;
    }
    else{

        perror(
            "connect"
        );
    }

    return 0;
}