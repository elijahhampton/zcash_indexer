#include <string>

#ifndef LOGGER_H
#define LOGGER_H

constexpr char RED[9] = "\033[31m";
constexpr char GREEN[9] = "\033[32m";
constexpr char YELLOW[9] = "\033[33m";
constexpr char BLUE[9] = "\033[34m";
constexpr char MAGENTA[9] = "\033[35m";
constexpr char CYAN[9] = "\033[36m";
constexpr char RESET[9] = "\033[0m";

void __DEBUG__(const char *);
void __LOG__(const char *);
void __ERROR__(const char *);

#endif // LOGGER_H