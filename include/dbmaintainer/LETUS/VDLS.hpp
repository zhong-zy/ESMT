#ifndef _VDLS_HPP_
#define _VDLS_HPP_

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace std;

namespace letus {

  class VDLS {
  public:

    VDLS(string file_path = "/mnt/c/Users/qyf/Desktop/LETUS_prototype/data/")
        : current_fileID_(0),
          current_offset_(0),
          file_path_(file_path),
          write_map_(MAP_FAILED),
          read_map_(MAP_FAILED),
          read_map_fileID_(-1) {
      OpenAndMapWriteFile();
    }

    tuple<uint64_t, uint64_t, uint64_t> WriteValue(uint64_t version,
                                                   const string& key,
                                                   const string& value) {
      string record = to_string(version) + "," + key + "," + value + "\n";
      size_t record_size = record.size();

      if (current_offset_ + record_size > MaxFileSize) {

        if (msync(write_map_, MaxFileSize, MS_SYNC) == -1) {
          throw runtime_error("Failed to sync changes to disk");
        }

        if (write_map_ != MAP_FAILED) {
          munmap(write_map_, MaxFileSize);
        }

        current_fileID_++;
        current_offset_ = 0;

        OpenAndMapWriteFile();
      }

      memcpy(static_cast<char*>(write_map_) + current_offset_, record.c_str(),
             record_size);

      current_offset_ += record_size;

      return make_tuple(current_fileID_, current_offset_ - record_size,
                        record_size);
    }

    tuple<uint64_t, uint64_t, uint64_t> WriteValueV1(
        uint64_t version, const string& key,
        const string& value) {
      string version_str = to_string(version);
      size_t record_size = version_str.size() + key.size() + value.size() + 3;
      if (current_offset_ + record_size > MaxFileSize) {
        current_fileID_++;
        current_offset_ = 0;
      }

      ofstream file(
          file_path_ + "data_file_" + to_string(current_fileID_) + ".dat",
          ios::app);
      if (!file) {
        throw std::runtime_error("Cannot open file ");
      }

      file << version_str << "," << key << "," << value << "\n";

      tuple<uint64_t, uint64_t, uint64_t> location(current_fileID_,
                                                   current_offset_, record_size);

      current_offset_ += record_size;

      file.close();

      return location;
    }

    string ReadValue(const tuple<uint64_t, uint64_t, uint64_t>& location) {
      uint64_t fileID, offset, size;
      tie(fileID, offset, size) = location;

      if (fileID != read_map_fileID_) {
        if (read_map_ != MAP_FAILED) {
          munmap(read_map_, MaxFileSize);
          read_map_ = MAP_FAILED;
        }
        OpenAndMapReadFile(fileID);
        read_map_fileID_ = fileID;
      }

      string line(static_cast<char*>(read_map_) + offset, size);

      stringstream ss(line);
      string temp, value;

      getline(ss, temp, ',');
      getline(ss, temp, ',');
      getline(ss, value);

      return value;
    }

    string ReadValueV1(const tuple<uint64_t, uint64_t, uint64_t>& location) {
      uint64_t fileID, offset, size;
      tie(fileID, offset, size) = location;

      ifstream file(file_path_ + "data_file_" + to_string(fileID) + ".dat");
      if (!file) {
        throw runtime_error("Cannot open file");
      }

      file.seekg(offset, ios::beg);

      string line;
      getline(file, line);

      stringstream ss(line);
      string temp, value;

      getline(ss, temp, ',');
      getline(ss, temp, ',');
      getline(ss, value);

      file.close();

      return value;
    }

  private:
    string file_path_;
    uint64_t current_fileID_;
    uint64_t current_offset_;
    const uint64_t MaxFileSize = 64 * 1024 * 1024;

    void* write_map_;
    void* read_map_;
    int64_t read_map_fileID_;

    void OpenAndMapWriteFile() {
      string filename =
          file_path_ + "data_file_" + to_string(current_fileID_) + ".dat";

      int fd = open(filename.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
      if (fd == -1) {
        throw runtime_error("Cannot open or create file: " + filename);
      }

      ftruncate(fd, MaxFileSize);

      write_map_ =
          mmap(nullptr, MaxFileSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if (write_map_ == MAP_FAILED) {
        close(fd);
        throw runtime_error("Memory map for writing failed: " + filename);
      }

      close(fd);
    }

    void OpenAndMapReadFile(uint64_t fileID) {
      string filename = file_path_ + "data_file_" + to_string(fileID) + ".dat";

      int fd = open(filename.c_str(), O_RDONLY);
      if (fd == -1) {
        throw runtime_error("Cannot open file for reading: " + filename);
      }

      read_map_ = mmap(nullptr, MaxFileSize, PROT_READ, MAP_SHARED, fd, 0);
      if (read_map_ == MAP_FAILED) {
        close(fd);
        throw runtime_error("Memory map for reading failed: " + filename);
      }

      close(fd);
    }
  };
}

#endif