#include "logger.h"
#include <iostream>

void __DEBUG__(const char* debug_info) {
    std::cout << BLUE << debug_info << std::endl;
}

void __LOG__(const char* log_info) {
    std::cout << GREEN << log_info << std::endl;
}

void __ERROR__(const char* error_info) {
    std::cout << RED << error_info << std::endl;
}