#ifndef LOGGER_H
#define LOGGER_H

constexpr char RED[] = "\033[31m";
constexpr char GREEN[] = "\033[32m";
constexpr char YELLOW[] = "\033[33m";
constexpr char BLUE[] = "\033[34m";
constexpr char MAGENTA[] = "\033[35m";
constexpr char CYAN[] = "\033[36m";
constexpr char RESET[] = "\033[0m";

void __DEBUG__(const char *);
void __ERROR__(const char *);
void __INFO__(const char *); 

#endif // LOGGER_H
