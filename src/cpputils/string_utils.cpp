
#include "cpputils/string_utils.h"
#include <iostream>

namespace cpputils {
    std::shared_ptr<std::vector<std::string>> string_split(const std::string &s, const std::string &&delimiters) {
        auto tokens = std::make_shared<std::vector<std::string>>();

        std::string::size_type last_pos = s.find_first_not_of(delimiters, 0);
        std::string::size_type pos = s.find_first_of(delimiters, last_pos);
        while (std::string::npos != pos || std::string::npos != last_pos) {
            tokens->push_back(s.substr(last_pos, pos-last_pos));
            last_pos = s.find_first_not_of(delimiters, pos);
            pos = s.find_first_of(delimiters, last_pos);
        }
        return tokens;
    }
}