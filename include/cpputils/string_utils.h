
#ifndef INDEX_MAINTAIN_STRING_UTILS_H
#define INDEX_MAINTAIN_STRING_UTILS_H

#include <string>
#include <vector>
#include <memory>

namespace cpputils {
    std::shared_ptr<std::vector<std::string>> string_split(const std::string &s, const std::string &&delimiters);
}

#endif
