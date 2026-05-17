#include "crypto.h"

#include "../v8_helpers.h"

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace kode { namespace v8embed {

namespace {

constexpr std::array<uint32_t, 64> kSha256Constants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t RotateRight(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32 - bits));
}

std::array<uint8_t, 32> Sha256(const std::string& input) {
    std::vector<uint8_t> bytes(input.begin(), input.end());
    const uint64_t bit_length = static_cast<uint64_t>(bytes.size()) * 8;
    bytes.push_back(0x80);
    while ((bytes.size() % 64) != 56) {
        bytes.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        bytes.push_back(static_cast<uint8_t>((bit_length >> (i * 8)) & 0xff));
    }

    std::array<uint32_t, 8> hash = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    for (size_t chunk = 0; chunk < bytes.size(); chunk += 64) {
        std::array<uint32_t, 64> words = {};
        for (size_t i = 0; i < 16; ++i) {
            const size_t offset = chunk + i * 4;
            words[i] = (static_cast<uint32_t>(bytes[offset]) << 24) |
                       (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
                       (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
                       static_cast<uint32_t>(bytes[offset + 3]);
        }
        for (size_t i = 16; i < 64; ++i) {
            const uint32_t s0 = RotateRight(words[i - 15], 7) ^ RotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const uint32_t s1 = RotateRight(words[i - 2], 17) ^ RotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = hash[0];
        uint32_t b = hash[1];
        uint32_t c = hash[2];
        uint32_t d = hash[3];
        uint32_t e = hash[4];
        uint32_t f = hash[5];
        uint32_t g = hash[6];
        uint32_t h = hash[7];

        for (size_t i = 0; i < 64; ++i) {
            const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + ch + kSha256Constants[i] + words[i];
            const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }

    std::array<uint8_t, 32> digest = {};
    for (size_t i = 0; i < hash.size(); ++i) {
        digest[i * 4] = static_cast<uint8_t>((hash[i] >> 24) & 0xff);
        digest[i * 4 + 1] = static_cast<uint8_t>((hash[i] >> 16) & 0xff);
        digest[i * 4 + 2] = static_cast<uint8_t>((hash[i] >> 8) & 0xff);
        digest[i * 4 + 3] = static_cast<uint8_t>(hash[i] & 0xff);
    }
    return digest;
}

std::string ToHex(const std::array<uint8_t, 32>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

void HashCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    std::string algorithm;
    if (!ReadStringArg(isolate, context, args, 0, "kode:crypto.hash", &algorithm)) return;
    std::string data;
    if (!ReadStringArg(isolate, context, args, 1, "kode:crypto.hash", &data)) return;

    if (algorithm != "sha256") {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EUNSUPPORTED_ALGORITHM", "Unsupported hash algorithm '" + algorithm + "'", "kode:crypto.hash", algorithm));
        return;
    }

    v8::Local<v8::Object> result = v8::Object::New(isolate);
    result->Set(context, V8String(isolate, "algorithm"), V8String(isolate, "sha256")).FromMaybe(false);
    result->Set(context, V8String(isolate, "hex"), V8String(isolate, ToHex(Sha256(data)))).FromMaybe(false);
    args.GetReturnValue().Set(result);
}

} // namespace

v8::Local<v8::Object> CreateCryptoModule(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> crypto = v8::Object::New(isolate);
    crypto->Set(context, V8String(isolate, "hash"), v8::Function::New(context, HashCallback).ToLocalChecked()).FromMaybe(false);
    return crypto;
}

} } // namespace kode::v8embed
