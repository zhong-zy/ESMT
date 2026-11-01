#ifndef _VDLS_HPP_
#define _VDLS_HPP_

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

class VDLS {
 public:
  VDLS(std::string file_path = "./")
      : current_fileID_(0), current_offset_(0), file_path_(file_path) {}

  std::tuple<uint64_t, uint64_t, uint64_t> WriteValue(
      uint64_t version, const std::string& key,
      const std::string& value) {
    std::string version_str = std::to_string(version);
    size_t record_size = version_str.size() + key.size() + value.size() + 3;
    if (current_offset_ + record_size > MaxFileSize) {
      current_fileID_++;
      current_offset_ = 0;
    }

    std::ofstream file(
        file_path_ + "data_file_" + std::to_string(current_fileID_) + ".dat",
        std::ios::app);
    if (!file) {
      throw std::runtime_error("Cannot open file ");
    }

    file << version_str << "," << key << "," << value << "\n";

    std::tuple<uint64_t, uint64_t, uint64_t> location(current_fileID_,
                                                 current_offset_, record_size);

    current_offset_ += record_size;

    file.close();

    return location;
  }

  std::string ReadValue(const std::tuple<uint64_t, uint64_t, uint64_t>& location) {
    uint64_t fileID, offset, size;
    std::tie(fileID, offset, size) = location;

    std::ifstream file(file_path_ + "data_file_" + std::to_string(fileID) + ".dat");
    if (!file) {
      throw std::runtime_error("Cannot open file");
    }

    file.seekg(offset, std::ios::beg);

    std::string line;
    std::getline(file, line);

    std::stringstream ss(line);
    std::string temp, value;

    std::getline(ss, temp, ',');
    std::getline(ss, temp, ',');
    std::getline(ss, value);

    file.close();

    return value;
  }

 private:
  std::string file_path_;
  uint64_t current_fileID_;
  uint64_t current_offset_;
  const uint64_t MaxFileSize = 64 * 1024 * 1024;
};

#endif