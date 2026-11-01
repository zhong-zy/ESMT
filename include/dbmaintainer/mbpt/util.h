
#ifndef MBPT_UTIL_H
#define MBPT_UTIL_H

#include <ctime>
#include <sys/time.h>
#include <string>

double getCurrentTime() {
    double res = 0.0;
    struct timeval tv;

    gettimeofday(&tv, nullptr);
    res += tv.tv_sec;
    res += (tv.tv_usec / 1000000.0);

    return res;
}

std::string getTimeStamp() {
    time_t timep;
    time(&timep);
    char buffer[80];
    auto timeinfo = localtime(&timep);
    strftime(buffer, 80, "%Y_%m_%d_%H_%M_%S", timeinfo);
    return buffer;
}

std::string randomString() {
    static const char alphanum[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    char s[10];
    for (char & i : s) {
        i = alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return s;
}

#endif
