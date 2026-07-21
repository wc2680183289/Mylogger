#include "space.h"

#include <iostream>

int main(){

    logger::bytes a(100);
    logger::kilobytes b(1);
    std::cout << logger::space(logger::bytes(a) + logger::bytes(b)).count() << std::endl;
    return 0;
}