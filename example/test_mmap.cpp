#include "mmap_aux.h"
#include "defer.h"

#include <string>
#include <iostream>

int main(){
    std::cout << "hello mmap" << std::endl;

    {
        LOG_DEFER{
            std::cout << "hello im defer" << std::endl;
        };
        std::cout << "hello defer" << std::endl;
    }

    logger::MMapAux mmap_aux("/home/wuchen/projects/Mylogger/example/test_mmap.txt");
    std::string str="hello mmap";
    mmap_aux.Push(str.data(), str.size());
    std::cout << mmap_aux.Size() << std::endl;
    std::cout << mmap_aux.Data() << std::endl;

    return 0;
}