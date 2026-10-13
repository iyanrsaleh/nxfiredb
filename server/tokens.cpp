#include "tokens.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

#include <nxfire/Json.h>

using nxfire::Json;

namespace nxfiretokens {

// --------------------------------------------------------------------------
// SHA-256 mandiri (publik domain, pola standar FIPS 180-4).
// --------------------------------------------------------------------------
std::string sha256Hex(const std::string& input) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x52380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<unsigned char> msg(input.begin(), input.end());
    uint64_t bitlen = uint64_t(msg.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back((unsigned char)((bitlen >> (i * 8)) & 0xff));

    auto rotr = [](uint32_t v, int b) { return (v >> b) | (v << (32 - b)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(msg[off + i*4]) << 24) | (uint32_t(msg[off + i*4 + 1]) << 16) |
                   (uint32_t(msg[off + i*4 + 2]) << 8) | uint32_t(msg[off + i*4 + 3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (~a & d);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }

    char out[65];
    for (int i = 0; i < 8; ++i)
        std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
}

// --------------------------------------------------------------------------
// Token acak: nxf_ + 32 hex (dari sha256 waktu + jam + counter).
// --------------------------------------------------------------------------
std::string generateToken() {
    static unsigned long counter = 0;
    ++counter;
    std::ostringstream seed;
    seed << (long long)std::time(nullptr) << ':' << counter << ':'
         << (uintptr_t)&counter << ':' << std::clock();
    return "nxf_" + sha256Hex(seed.str()).substr(0, 32);
}

// --------------------------------------------------------------------------
// TokenStore
// --------------------------------------------------------------------------
TokenStore::TokenStore(const std::string& filePath) : path_(filePath) {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return;
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string err;
    Json j = Json::parse(ss.str(), &err);
    if (err.empty() && j.isObject() && j.contains("tokens") && j.at("tokens").isArray()) {
        for (const auto& t : j.at("tokens").asArray()) {
            TokenInfo info;
            info.user      = t.at("user").asString();
            info.role      = t.at("role").asString();
            info.tokenHash = t.at("token_hash").asString();
            info.createdAt = t.contains("created_at") ? t.at("created_at").asInt(0) : 0;
            if (!info.user.empty() && !info.tokenHash.empty())
                tokens_.push_back(info);
        }
    }
}

bool TokenStore::save() const {
    Json j = Json::object();
    j.set("version", Json(int64_t(1)));
    Json arr = Json::array();
    for (const auto& t : tokens_) {
        Json o = Json::object();
        o.set("user", Json(t.user));
        o.set("role", Json(t.role));
        o.set("token_hash", Json(t.tokenHash));
        o.set("created_at", Json((int64_t)t.createdAt));
        arr.push_back(o);
    }
    j.set("tokens", arr);
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << j.dump();
    return true;
}

bool TokenStore::empty() const { return tokens_.empty(); }

std::string TokenStore::create(const std::string& user, const std::string& role) {
    for (const auto& t : tokens_)
        if (t.user == user) return "";
    TokenInfo info;
    info.user = user;
    info.role = (role == "admin") ? "admin" : "user";
    std::string token = generateToken();
    info.tokenHash = sha256Hex(token);
    info.createdAt = (long long)std::time(nullptr);
    tokens_.push_back(info);
    return token;
}

std::string TokenStore::rotate(const std::string& user) {
    for (auto& t : tokens_) {
        if (t.user == user) {
            std::string token = generateToken();
            t.tokenHash = sha256Hex(token);
            t.createdAt = (long long)std::time(nullptr);
            return token;
        }
    }
    return "";
}

bool TokenStore::remove(const std::string& user) {
    for (size_t i = 0; i < tokens_.size(); ++i) {
        if (tokens_[i].user == user) {
            tokens_.erase(tokens_.begin() + i);
            return true;
        }
    }
    return false;
}

std::vector<TokenInfo> TokenStore::list() const { return tokens_; }

bool TokenStore::authenticate(const std::string& token, std::string& user,
                              std::string& role) const {
    if (token.empty()) return false;
    std::string hash = sha256Hex(token);
    for (const auto& t : tokens_) {
        if (t.tokenHash == hash) {
            user = t.user;
            role = t.role;
            return true;
        }
    }
    return false;
}

std::string TokenStore::ensureOwner() {
    if (!tokens_.empty()) return "";
    return create("__owner__", "admin");
}

} // namespace nxfiretokens
