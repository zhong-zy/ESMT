
#ifndef MBPT_MERKLE_H
#define MBPT_MERKLE_H

#include <iostream>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <sstream>
#include <stack>
#include <vector>

#ifdef HAVE_OPENSSL
#  include <openssl/evp.h>
#  include <openssl/sha.h>
#endif
#define SSL_SHA256

#ifdef HAVE_MBEDTLS
#  include <mbedtls/sha256.h>
#endif

#ifdef MERKLECPP_TRACE_ENABLED

#  define TRACE_HASH_SIZE 3

#  ifndef MERKLECPP_TRACE
#    include <iostream>
#    define MERKLECPP_TOUT std::cout
#    define MERKLECPP_TRACE(X) \
      { \
        X; \
        MERKLECPP_TOUT.flush(); \
      };
#  endif
#else
#  define MERKLECPP_TRACE(X)
#endif

#define MERKLECPP_VERSION_MAJOR 1
#define MERKLECPP_VERSION_MINOR 0
#define MERKLECPP_VERSION_PATCH 0

namespace merkle
{
    static inline uint32_t convert_endianness(uint32_t n)
    {
        const uint32_t sz = sizeof(uint32_t);
#if defined(htobe32)

        return htobe32(n);
#elif defined(__LITTLE_ENDIAN__) || defined(__LITTLE_ENDIAN)

    uint32_t r = 0;
    for (size_t i = 0; i < sz; i++)
      r |= ((n >> (8 * ((sz - 1) - i))) & 0xFF) << (8 * i);
    return *reinterpret_cast<uint32_t*>(&r);
#else

    uint8_t r[8];
    for (size_t i = 0; i < sz; i++)
      r[i] = (n >> (8 * ((sz - 1) - i))) & 0xFF;
    return *reinterpret_cast<uint32_t*>(&r);
#endif
    }

    static inline void serialise_uint64_t(uint64_t n, std::vector<uint8_t>& bytes)
    {
        size_t sz = sizeof(uint64_t);
        bytes.reserve(bytes.size() + sz);
        for (uint64_t i = 0; i < sz; i++)
            bytes.push_back((n >> (8 * (sz - i - 1))) & 0xFF);
    }

    static inline uint64_t deserialise_uint64_t(
            const std::vector<uint8_t>& bytes, size_t& index)
    {
        uint64_t r = 0;
        uint64_t sz = sizeof(uint64_t);
        for (uint64_t i = 0; i < sz; i++)
            r |= static_cast<uint64_t>(bytes.at(index++)) << (8 * (sz - i - 1));
        return r;
    }

    template <size_t SIZE>
    struct HashT
    {

        uint8_t bytes[SIZE];

        HashT<SIZE>()
        {
            std::fill(bytes, bytes + SIZE, 0);
        }

        HashT<SIZE>(const uint8_t* bytes)
        {
            std::copy(bytes, bytes + SIZE, this->bytes);
        }

        HashT<SIZE>(const std::string& s)
        {
#ifndef SSL_SHA256
            if (s.length() != 2 * SIZE)
                throw std::runtime_error("invalid hash string");
            for (size_t i = 0; i < SIZE; i++)
            {
                int tmp;
                sscanf(s.c_str() + 2 * i, "%02x", &tmp);
                bytes[i] = tmp;
            }
#else
            for (size_t i = 0; i < SIZE; i++) {
                bytes[i] = s[i];
            }
#endif
        }

        HashT<SIZE>(const std::vector<uint8_t>& bytes)
        {
            if (bytes.size() < SIZE)
                throw std::runtime_error("not enough bytes");
            deserialise(bytes);
        }

        HashT<SIZE>(const std::vector<uint8_t>& bytes, size_t& position)
        {
            if (bytes.size() - position < SIZE)
                throw std::runtime_error("not enough bytes");
            deserialise(bytes, position);
        }

        HashT<SIZE>(const std::array<uint8_t, SIZE>& bytes)
        {
            std::copy(bytes.data(), bytes.data() + SIZE, this->bytes);
        }

        size_t size() const
        {
            return SIZE;
        }

        void zero()
        {
            std::fill(bytes, bytes + SIZE, 0);
        }

        size_t serialised_size() const
        {
            return SIZE;
        }

        std::string to_string(size_t num_bytes = SIZE, bool lower_case = true) const
        {
            size_t num_chars = 2 * num_bytes;
            std::string r(num_chars, '_');
            for (size_t i = 0; i < num_bytes; i++)
                snprintf(
                        const_cast<char*>(r.data() + 2 * i),
                        num_chars + 1 - 2 * i,
                        lower_case ? "%02x" : "%02X",
                        bytes[i]);
            return r;
        }

        HashT<SIZE> operator=(const HashT<SIZE>& other)
        {
            std::copy(other.bytes, other.bytes + SIZE, bytes);
            return *this;
        }

        bool operator==(const HashT<SIZE>& other) const
        {
            return memcmp(bytes, other.bytes, SIZE) == 0;
        }

        bool operator!=(const HashT<SIZE>& other) const
        {
            return memcmp(bytes, other.bytes, SIZE) != 0;
        }

        void serialise(std::vector<uint8_t>& buffer) const
        {
            MERKLECPP_TRACE(MERKLECPP_TOUT << "> HashT::serialise " << std::endl);
            for (auto& b : bytes)
                buffer.push_back(b);
        }

        void deserialise(const std::vector<uint8_t>& buffer, size_t& position)
        {
            MERKLECPP_TRACE(MERKLECPP_TOUT << "> HashT::deserialise " << std::endl);
            if (buffer.size() - position < SIZE)
                throw std::runtime_error("not enough bytes");
            for (size_t i = 0; i < sizeof(bytes); i++)
                bytes[i] = buffer[position++];
        }

        void deserialise(const std::vector<uint8_t>& buffer)
        {
            size_t position = 0;
            deserialise(buffer, position);
        }

        operator std::vector<uint8_t>() const
        {
            std::vector<uint8_t> bytes;
            serialise(bytes);
            return bytes;
        }
    };

    template <
            size_t HASH_SIZE,
            void HASH_FUNCTION(
                    const HashT<HASH_SIZE>& l,
                    const HashT<HASH_SIZE>& r,
                    HashT<HASH_SIZE>& out)>
    class PathT
    {
    public:

        typedef enum
        {
            PATH_LEFT,
            PATH_RIGHT
        } Direction;

        typedef struct
        {

            HashT<HASH_SIZE> hash;

            Direction direction;
        } Element;

        PathT(

                const HashT<HASH_SIZE> leaf,
                std::list<Element> elements):
                _leaf(leaf),
                elements(elements)
        {}

        PathT(const PathT& other)
        {
            _leaf = other._leaf;
            elements = other.elements;
        }

        PathT(PathT&& other)
        {
            _leaf = std::move(other._leaf);
            elements = std::move(other.elements);
        }

        PathT(const std::vector<uint8_t>& bytes)
        {
            deserialise(bytes);
        }

        PathT(const std::vector<uint8_t>& bytes, size_t& position)
        {
            deserialise(bytes, position);
        }

        std::shared_ptr<HashT<HASH_SIZE>> root() const
        {
            std::shared_ptr<HashT<HASH_SIZE>> result =
                    std::make_shared<HashT<HASH_SIZE>>(_leaf);
            MERKLECPP_TRACE(
                    MERKLECPP_TOUT << "> PathT::root " << _leaf.to_string(TRACE_HASH_SIZE)
                                   << std::endl);

            for (const Element& e : elements)
            {
                if (e.direction == PATH_LEFT)
                {
                    MERKLECPP_TRACE(
                            MERKLECPP_TOUT << " - " << e.hash.to_string(TRACE_HASH_SIZE)
                                           << " x " << result->to_string(TRACE_HASH_SIZE)
                                           << std::endl);
                    HASH_FUNCTION(e.hash, *result, *result);
                }
                else
                {
                    MERKLECPP_TRACE(
                            MERKLECPP_TOUT << " - " << result->to_string(TRACE_HASH_SIZE)
                                           << " x " << e.hash.to_string(TRACE_HASH_SIZE)
                                           << std::endl);
                    HASH_FUNCTION(*result, e.hash, *result);
                }

            }
            MERKLECPP_TRACE(
                    MERKLECPP_TOUT << " = " << result->to_string(TRACE_HASH_SIZE)
                                   << std::endl);
            return result;
        }

        bool verify(const HashT<HASH_SIZE>& expected_root) const
        {
            return *root() == expected_root;
        }

        void serialise(std::vector<uint8_t>& bytes) const
        {
            MERKLECPP_TRACE(MERKLECPP_TOUT << "> PathT::serialise " << std::endl);
            _leaf.serialise(bytes);
            serialise_uint64_t(elements.size(), bytes);
            for (auto& e : elements)
            {
                e.hash.serialise(bytes);
                bytes.push_back(e.direction == PATH_LEFT ? 1 : 0);
            }
        }

        void deserialise(const std::vector<uint8_t>& bytes, size_t& position)
        {
            MERKLECPP_TRACE(MERKLECPP_TOUT << "> PathT::deserialise " << std::endl);
            elements.clear();
            _leaf.deserialise(bytes, position);
            size_t num_elements = deserialise_uint64_t(bytes, position);
            for (size_t i = 0; i < num_elements; i++)
            {
                HashT<HASH_SIZE> hash(bytes, position);
                PathT::Direction direction =
                        bytes.at(position++) != 0 ? PATH_LEFT : PATH_RIGHT;
                PathT::Element e;
                e.hash = hash;
                e.direction = direction;
                elements.push_back(std::move(e));
            }
        }

        void deserialise(const std::vector<uint8_t>& bytes)
        {
            size_t position = 0;
            deserialise(bytes, position);
        }

        operator std::vector<uint8_t>() const
        {
            std::vector<uint8_t> bytes;
            serialise(bytes);
            return bytes;
        }

        size_t size() const
        {
            return elements.size();
        }

        size_t serialised_size() const
        {
            return sizeof(_leaf) +
                   sizeof(uint64_t) +
                   sizeof(uint64_t) +
                   sizeof(uint64_t) +
                   elements.size() * (
                           sizeof(Element::hash) +
                           sizeof(uint8_t)
                   );
        }

        const HashT<HASH_SIZE>& operator[](size_t i) const
        {
            return std::next(begin(), i)->hash;
        }

        typedef typename std::list<Element>::const_iterator const_iterator;

        const_iterator begin() const
        {
            return elements.begin();
        }

        const_iterator end() const
        {
            return elements.end();
        }

        std::string to_string(
                size_t num_bytes = HASH_SIZE, bool lower_case = true) const
        {
            std::stringstream stream;
            stream << _leaf.to_string(num_bytes);
            for (auto& e : elements)
                stream << " " << e.hash.to_string(num_bytes, lower_case)
                       << (e.direction == PATH_LEFT ? "(L)" : "(R)");
            return stream.str();
        }

        const HashT<HASH_SIZE>& leaf() const
        {
            return _leaf;
        }

        bool operator==(const PathT<HASH_SIZE, HASH_FUNCTION>& other) const
        {
            if (_leaf != other._leaf || elements.size() != other.elements.size())
                return false;
            auto it = elements.begin();
            auto other_it = other.elements.begin();
            while (it != elements.end() && other_it != other.elements.end())
            {
                if (it->hash != other_it->hash || it->direction != other_it->direction)
                    return false;
                it++;
                other_it++;
            }
            return true;
        }

        bool operator!=(const PathT<HASH_SIZE, HASH_FUNCTION>& other)
        {
            return !this->operator==(other);
        }

    public:

        HashT<HASH_SIZE> _leaf;

        std::list<Element> elements;
    };

    static inline void sha256_compress(const HashT<32> &l, const HashT<32> &r, HashT<32> &out) {
        static const uint32_t constants[] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        uint8_t block[32 * 2];
        memcpy(&block[0], l.bytes, 32);
        memcpy(&block[32], r.bytes, 32);

        static const uint32_t s[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

        uint32_t cws[64] = {0};

        for (int i=0; i < 16; i++)
            cws[i] = convert_endianness(((int32_t *)block)[i]);

        for (int i = 16; i < 64; i++) {
            uint32_t t16 = cws[i - 16];
            uint32_t t15 = cws[i - 15];
            uint32_t t7 = cws[i - 7];
            uint32_t t2 = cws[i - 2];
            uint32_t s1 = (t2 >> 17 | t2 << 15) ^ ((t2 >> 19 | t2 << 13) ^ t2 >> 10);
            uint32_t s0 = (t15 >> 7 | t15 << 25) ^ ((t15 >> 18 | t15 << 14) ^ t15 >> 3);
            cws[i] = (s1 + t7 + s0 + t16);
        }

        uint32_t h[8];
        for (int i=0; i < 8; i++)
            h[i] = s[i];

        for (int i=0; i < 64; i++) {
            uint32_t a0 = h[0], b0 = h[1], c0 = h[2], d0 = h[3], e0 = h[4], f0 = h[5], g0 = h[6], h03 = h[7];
            uint32_t w = cws[i];
            uint32_t t1 = h03 + ((e0 >> 6 | e0 << 26) ^ ((e0 >> 11 | e0 << 21) ^ (e0 >> 25 | e0 << 7))) + ((e0 & f0) ^ (~e0 & g0)) + constants[i] + w;
            uint32_t t2 = ((a0 >> 2 | a0 << 30) ^ ((a0 >> 13 | a0 << 19) ^ (a0 >> 22 | a0 << 10))) + ((a0 & b0) ^ ((a0 & c0) ^ (b0 & c0)));
            h[0] = t1 + t2;
            h[1] = a0;
            h[2] = b0;
            h[3] = c0;
            h[4] = d0 + t1;
            h[5] = e0;
            h[6] = f0;
            h[7] = g0;
        }

        for (int i=0; i < 8; i++)
            ((uint32_t*)out.bytes)[i] = convert_endianness(s[i] + h[i]);
    }

#ifdef HAVE_OPENSSL

  static inline void sha256_openssl(
    const merkle::HashT<32>& l,
    const merkle::HashT<32>& r,
    merkle::HashT<32>& output)
  {
    uint8_t block[32 * 2];
    memcpy(&block[0], l.bytes, 32);
    memcpy(&block[32], r.bytes, 32);

    const EVP_MD* md = EVP_sha256();
    int rc =
      EVP_Digest(&block[0], sizeof(block), output.bytes, nullptr, md, nullptr);
    if (rc != 1)
    {
      throw std::runtime_error("EVP_Digest failed: " + std::to_string(rc));
    }
  }
#endif

#ifdef HAVE_MBEDTLS

  static inline void sha256_compress_mbedtls(
    const HashT<32>& l, const HashT<32>& r, HashT<32>& output)
  {
    unsigned char block[32 * 2];
    memcpy(&block[0], l.bytes, 32);
    memcpy(&block[32], r.bytes, 32);

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, false);
    mbedtls_internal_sha256_process(&ctx, &block[0]);

    for (int i = 0; i < 8; i++)
      ((uint32_t*)output.bytes)[i] = htobe32(ctx.state[i]);
  }

  static inline void sha256_mbedtls(
    const merkle::HashT<32>& l,
    const merkle::HashT<32>& r,
    merkle::HashT<32>& output)
  {
    uint8_t block[32 * 2];
    memcpy(&block[0], l.bytes, 32);
    memcpy(&block[32], r.bytes, 32);
    mbedtls_sha256_ret(block, sizeof(block), output.bytes, false);
  }
#endif

    typedef HashT<32> Hash;

    typedef PathT<32, sha256_compress> Path;

#define M_LEN 64
#define MAX_LEN (M_LEN << 4)
#define Rn(X, n) ((X << (32 - n)) | (X >> n))
#define Sn(X, n) (X >> n)
#define Ch(X, Y, Z) ((X & Y) ^ (~X & Z))
#define Maj(X, Y, Z) ((X & Y) ^ (X & Z) ^ (Y & Z))
#define Sigma_E0(X) (Rn(X, 2) ^ Rn(X, 13) ^ Rn(X, 22))
#define Sigma_E1(X) (Rn(X, 6) ^ Rn(X, 11) ^ Rn(X, 25))
#define Sigma_o0(X) (Rn(X, 7) ^ Rn(X, 18) ^ Sn(X, 3))
#define Sigma_o1(X) (Rn(X, 17) ^ Rn(X, 19) ^ Sn(X, 10))

    static void uint_to_hash(Hash &hash, const unsigned int (&H)[8]) {
        for (int i = 0; i < 8; i++) {
            hash.bytes[i * 4 + 0] = (uint8_t) ((H[i] & 0xFF000000) >> 24);
            hash.bytes[i * 4 + 1] = (uint8_t) ((H[i] & 0x00FF0000) >> 16);
            hash.bytes[i * 4 + 2] = (uint8_t) ((H[i] & 0x0000FF00) >> 8);
            hash.bytes[i * 4 + 3] = (uint8_t) ((H[i] & 0x000000FF));
        }
    }

    static Hash SHA256(const std::string &ori) {

        unsigned int K[48] = {
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };
        unsigned int *Kp, *W, *M, a, b, c, d, e, f, g, h, T1;
        unsigned int H[8];
        H[0] = 0x6a09e667, H[1] = 0xbb67ae85, H[2] = 0x3c6ef372, H[3] = 0xa54ff53a,
        H[4] = 0x510e527f, H[5] = 0x9b05688c, H[6] = 0x1f83d9ab, H[7] = 0x5be0cd19;
        char *name, *ch, tmp1, tmp2;
        size_t i = 0;
        for (ch = name = (char *) calloc(MAX_LEN, 1); i < ori.size(); *ch = ori[i], ch++, i++);
        *ch = '\0';

        a = ch - name;
        b = a & 63;
        b = a + ((b < 56) ? (M_LEN - b) : (M_LEN | (M_LEN - b)));
        ch = (char *) realloc(name, b + 254);
        ch[a] = 0x80;
        name = ch + (a | 3);
        do {
            tmp1 = *name;
            *name = name[-3];
            name--;
            tmp2 = *name;
            *name = name[-1];
            name--;
            *name = tmp2;
            name--;
            *name = tmp1;
            name--;
        } while (ch < name);
        M = (unsigned int *) ch;
        b >>= 2;
        W = M + b;
        W[-2] = a >> 29;
        W[-1] = a << 3;
        do {
            a = H[0];
            b = H[1];
            c = H[2];
            d = H[3];
            e = H[4];
            f = H[5];
            g = H[6];
            h = H[7];
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x428a2f98;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x71374491;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0xb5c0fbcf;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0xe9b5dba5;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x3956c25b;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x59f111f1;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x923f82a4;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0xab1c5ed5;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0xd807aa98;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x12835b01;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x243185be;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x550c7dc3;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x72be5d74;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x80deb1fe;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0x9bdc06a7;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            *W = *M;
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + 0xc19bf174;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            W++;
            M++;
            Kp = K;
            do {
                *W = Sigma_o1(W[-2]) + W[-7] + Sigma_o0(W[-15]) + W[-16];
                T1 = h + Sigma_E1(e) + Ch(e, f, g) + *W + *Kp;
                h = g;
                g = f;
                f = e;
                e = d + T1;
                d = c;
                c = b;
                b = a;
                a = T1 + Sigma_E0(b) + Maj(b, c, d);
                W++;
                Kp++;
            } while (Kp != &K[46]);
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + Sigma_o1(W[-2]) + W[-7] + Sigma_o0(W[-15]) + W[-16] + 0xbef9a3f7;
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + Sigma_E0(b) + Maj(b, c, d);
            T1 = h + Sigma_E1(e) + Ch(e, f, g) + Sigma_o1(W[-1]) + W[-6] + Sigma_o0(W[-14]) + W[-15] + 0xc67178f2;
            H[1] += a;
            H[2] += b;
            H[3] += c;
            H[4] += d + T1;
            H[5] += e;
            H[6] += f;
            H[7] += g;
            H[0] += T1 + Sigma_E0(a) + Maj(a, b, c);
            W -= 62;
        } while (M != W);

        Hash ret;
        uint_to_hash(ret, H);
        return ret;
    }

};

#endif
