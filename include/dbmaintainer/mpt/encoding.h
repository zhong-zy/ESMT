
#ifndef SHRUBS_LIKE_INDEX_ENCODING_H
#define SHRUBS_LIKE_INDEX_ENCODING_H

#include <vector>
#include <iostream>

namespace mpt {

    static std::vector<uint8_t> KeyBytesToHex(const std::vector<uint8_t> &input){

        auto l = input.size() * 2;
        std::vector<uint8_t> nibbles(l);
        for (int i = 0; i < input.size(); i++){
            nibbles[i * 2] = input[i] / 16;
            nibbles[i * 2 + 1] = input[i] % 16;
        }

        return nibbles;
    }

    static std::vector<uint8_t> KeyBytesToHex(const std::string &key){
        std::vector<uint8_t> input = {key.begin(), key.end()};
        return KeyBytesToHex(input);
    }

    static std::vector<uint8_t> ToPrefixed(const std::vector<uint8_t> &ns, bool is_leaf) {
        std::vector<uint8_t> prefix_bytes;
        if (ns.size() %2 > 0) {
            prefix_bytes = {1};
        } else {
            prefix_bytes = {0, 0};
        }
        std::vector<uint8_t> prefixed;
        std::copy(prefix_bytes.begin(), prefix_bytes.end(), prefixed.end());
        std::copy(ns.begin(), ns.end(), prefixed.end());
        if (is_leaf) {
            prefixed[0] += 2;
        }
        return prefixed;
    }

    static std::vector<uint8_t> HexToBytes(const std::vector<uint8_t> &hex) {
        std::vector<uint8_t> buf;
        for (int i = 0; i < hex.size(); i += 2) {
            buf.push_back((hex[i] << 4) + hex[i+1]);
        }
        return buf;
    }

    static std::string HexToBytesString(const std::vector<uint8_t> &hex) {
        auto buf = HexToBytes(hex);
        return {buf.begin(), buf.end()};
    }

    static
    int PrefixLen(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b){
        int i = 0;
        auto length = a.size();
        if (b.size() < length){
            length = b.size();
        }
        while (i < length){
            if (a[i] != b[i]){
                break;
            }
            i++;
        }
        return i;
    };
}

#endif
