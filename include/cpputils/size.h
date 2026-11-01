
#ifndef SHRUBS_LIKE_INDEX_SIZE_H
#define SHRUBS_LIKE_INDEX_SIZE_H

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

namespace cpputils {
    static std::string getSizeStr(double bytes) {
        std::string unit = "B";
        if (bytes >= 1024.0) {
            bytes /= 1024.0;
            unit = "KB";
            if (bytes >= 1024.0) {
                bytes /= 1024.0;
                unit = "MB";
                if (bytes >= 1024.0) {
                    bytes /= 1024.0;
                    unit = "GB";
                }
            }
        }
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << bytes << " " << unit;
        return ss.str();
    }
}

#endif
