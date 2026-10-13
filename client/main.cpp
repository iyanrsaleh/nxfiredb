// nxfire-client
// Kirim perintah JSON ke NxfireDB.
//
//   nxfire-client '{"op":"get","path":"/users"}' --config nxfire.json
//
// Tampilan memakai palet warna terminal Ubuntu (aksen oranye #E95420) saat
// dijalankan di terminal (TTY). Bila output dialihkan (pipe/file), JSON tetap
// keluar ringkas tanpa warna supaya aman untuk skrip.

#include <nxfire/Nxfire.h>
#include <nxfire/Http.h>
#include <nxfire/Theme.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace T = nxfire::theme;

using namespace nxfire;

namespace {

// --------------------------------------------------------------------------
// Palet warna terminal Ubuntu (truecolor) - dari nxfire/Theme.h
// --------------------------------------------------------------------------
const char* RST     = T::RESET;
const char* ORANGE  = T::ORANGE;
const char* GREEN   = T::GREEN;
const char* RED     = T::RED;
const char* BLUE    = T::BLUE;
const char* CYAN    = T::CYAN;
const char* YELLOW  = T::YELLOW;
const char* MAGENTA = T::MAGENTA;
const char* GRAY    = T::GRAY;
const char* WHITE   = T::WHITE;

bool gColor = false;

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string C(const char* code) { return T::on() ? std::string(code) : std::string(); }
std::string C(const char* code, const std::string& s) {
    return T::on() ? std::string(code) + s + RST : s;
}

// --------------------------------------------------------------------------
// Pretty-print JSON ringkas -> berindentasi.
// --------------------------------------------------------------------------
std::string pretty(const std::string& s) {
    std::string out;
    int ind = 0;
    bool inStr = false, esc = false;
    auto nl = [&]() { out.push_back('\n'); out.append(size_t(ind) * 2, ' '); };

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) {
            out.push_back(c);
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        switch (c) {
            case '"': inStr = true; out.push_back(c); break;
            case '{': case '[': {
                size_t j = i + 1;
                while (j < s.size() && s[j] == ' ') ++j;
                char close = (c == '{') ? '}' : ']';
                if (j < s.size() && s[j] == close) { out.push_back(c); out.push_back(close); i = j; break; }
                ++ind; out.push_back(c); nl(); break;
            }
            case '}': case ']':
                if (ind > 0) --ind;
                nl(); out.push_back(c); break;
            case ',': out.push_back(c); nl(); break;
            case ':': out += ": "; break;
            case ' ': break;
            default: out.push_back(c);
        }
    }
    return out;
}

// --------------------------------------------------------------------------
// Warnai JSON memakai palet Ubuntu.
// --------------------------------------------------------------------------
std::string colorize(const std::string& s) {
    std::string out;
    std::string lastKey;
    size_t i = 0, n = s.size();

    while (i < n) {
        char c = s[i];

        if (c == '"') {
            size_t j = i + 1;
            bool esc = false;
            while (j < n) {
                char d = s[j];
                if (esc) esc = false;
                else if (d == '\\') esc = true;
                else if (d == '"') break;
                ++j;
            }
            std::string tok = s.substr(i, j - i + 1);
            size_t k = j + 1;
            while (k < n && s[k] == ' ') ++k;
            if (k < n && s[k] == ':') {
                lastKey = tok.substr(1, tok.size() - 2);
                out += C(CYAN, tok);
            } else if (lastKey == "error") {
                out += C(RED, tok);
            } else if (lastKey == "hint") {
                out += C(GRAY, tok);
            } else if (lastKey == "ok") {
                out += C(tok == "\"true\"" ? GREEN : RED, tok);
            } else {
                out += C(YELLOW, tok);
            }
            i = j + 1;
            continue;
        }

        if (c == 't' || c == 'f' || c == 'n') {
            size_t len = (c == 'n') ? 4 : ((c == 't') ? 4 : 5);
            std::string tok = s.substr(i, len);
            if (tok == "null") out += C(GRAY, tok);
            else if (lastKey == "ok") out += C(tok == "true" ? GREEN : RED, tok);
            else out += C(ORANGE, tok);
            i += len;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            size_t j = i;
            while (j < n && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.' ||
                             s[j] == '-' || s[j] == '+' || s[j] == 'e' || s[j] == 'E'))
                ++j;
            std::string tok = s.substr(i, j - i);
            const char* col = (lastKey == "latency_ms" || lastKey == "http_status") ? CYAN : MAGENTA;
            out += C(col, tok);
            i = j;
            continue;
        }

        out.push_back(c);
        ++i;
    }
    return out;
}

// --------------------------------------------------------------------------
// Tampilkan hasil.
// --------------------------------------------------------------------------
void render(const std::string& jsonText, const std::string& op,
            const std::string& target, const std::string& pHost,
            bool ok, int latency) {
    if (!gColor) {
        std::printf("%s\n", jsonText.c_str());
        return;
    }

    std::string o;
    o += "\n";
    o += C(GREEN, "nxfire@" + pHost);
    o += C(GRAY, ":");
    o += C(BLUE, "~");
    o += C(WHITE, "$");
    o += " " + C(GRAY, op) + "\n\n";

    o += C(ORANGE, "NXFIREDB");
    o += "  " + C(WHITE, op);
    if (!target.empty()) o += C(GRAY, "  " + target);
    o += "\n\n";

    o += colorize(pretty(jsonText));
    o += "\n\n";

    o += ok ? C(GREEN, "ok") : C(RED, "error");
    if (latency >= 0) o += C(GRAY, "  " + std::to_string(latency) + " ms");
    o += "\n";

    std::fwrite(o.data(), 1, o.size(), stdout);
}

std::string stripScheme(std::string u) {
    for (const char* p : {"https://", "http://"}) {
        size_t n = std::strlen(p);
        if (u.compare(0, n, p) == 0) { u = u.substr(n); break; }
    }
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

void printUsage() {
    std::printf(
        "nxfire-client - send JSON commands to NxfireDB\n\n"
        "Usage:\n"
        "  nxfire-client '<json>' [--config FILE] [--host H] [--port N]\n\n"
        "Examples:\n"
        "  nxfire-client '{\"op\":\"set\",\"path\":\"/users/budi\","
        "\"value\":{\"name\":\"Budi\",\"age\":30}}'\n"
        "  nxfire-client '{\"op\":\"get\",\"path\":\"/users\"}'\n"
        "  nxfire-client '{\"op\":\"push\",\"path\":\"/chat\","
        "\"value\":{\"text\":\"halo\"}}'\n"
        "  nxfire-client '{\"op\":\"set\",\"table\":\"users\",\"key\":\"budi\","
        "\"value\":{\"name\":\"Budi\"}}'\n\n"
        "Operations: set, get, update, push, remove\n"
        "            (aliases: insert, select, delete)\n"
        "Options: --config FILE        (default nxfire.json)\n"
        "         --port N             send to the local server\n"
        "         --token TOKEN        access token for the local server\n"
        "         --token-file FILE    read the token from a file (first line)\n"
        "         --color auto|always|never\n"
        "         --plain              same as --color never\n\n"
        "Token can also come from the NXFIRE_TOKEN environment variable.\n"
        "Example:\n"
        "  nxfire-client '{\"op\":\"get\",\"path\":\"/users\"}' --port 9026 \\\n"
        "    --token nxf_0123456789abcdef0123456789abcdef\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string opJson;
    std::string config = "nxfire.json";
    std::string host = "127.0.0.1";
    std::string token;                 // "Bearer nxf_..." (mode server)
    std::string tokenFile;             // baca token dari file
    int port = 0;
    int colorMode = 0; // 0=auto, 1=always, 2=never

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "option %s needs a value\n", name); std::exit(2); }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { printUsage(); return 0; }
        else if (a == "--config") config = next("--config");
        else if (a == "--host")   host = next("--host");
        else if (a == "--port")   port = std::stoi(next("--port"));
        else if (a == "--token")  token = next("--token");
        else if (a == "--token-file") tokenFile = next("--token-file");
        else if (a == "--plain" || a == "--no-color") colorMode = 2;
        else if (a == "--color") {
            std::string v = next("--color");
            colorMode = (v == "always") ? 1 : (v == "never") ? 2 : 0;
        }
        else if (a.rfind("-", 0) == 0) { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
        else opJson = a;
    }

    if (opJson.empty()) { printUsage(); return 2; }

    // Token: --token menang; --token-file membaca file berisi token saja;
    // fallback: NXFIRE_TOKEN dari lingkungan.
    if (token.empty() && !tokenFile.empty()) {
        std::ifstream in(tokenFile, std::ios::binary);
        if (in) { std::getline(in, token); token = trim(token); }
    }
    if (token.empty()) {
        const char* envTok = std::getenv("NXFIRE_TOKEN");
        if (envTok) token = envTok;
    }

    gColor = (colorMode == 1) || (colorMode == 0 && T::stdoutIsTty() && !T::envNoColor());
    T::set(gColor);

    // Muat op/target untuk header (best-effort).
    std::string perr;
    Json req = Json::parse(opJson, &perr);
    std::string op, target;
    if (req.isObject()) {
        op = req.at("op").asString();
        if (!req.at("path").asString().empty()) target = req.at("path").asString();
        else if (!req.at("table").asString().empty()) target = "/" + req.at("table").asString();
    }

    // Mode server: kirim op ke nxfire-server lokal (--port diberikan).
    if (port > 0) {
        const std::string pHost = host + ":" + std::to_string(port);
        const std::string url = "http://" + pHost + "/";
        std::vector<std::pair<std::string, std::string>> headers;
        if (!token.empty()) headers.push_back({"Authorization", "Bearer " + token});
        http::Response res = http::request("POST", url, opJson, headers);
        if (!res.error.empty()) {
            std::string j = "{\"ok\":false,\"error\":\"network_error\",\"hint\":\"" + res.error + "\"}";
            render(j, op, target, pHost, false, -1);
            return 1;
        }
        bool ok = (res.status >= 200 && res.status < 300);
        render(res.body, op, target, pHost, ok, -1);
        return ok ? 0 : 1;
    }

    if (!perr.empty() || !req.isObject()) {
        std::string j = "{\"ok\":false,\"error\":\"bad_request\",\"hint\":\"JSON tidak valid: " + perr + "\"}";
        render(j, op, target, "nxfire", false, -1);
        return 1;
    }

    Nxfire db;
    if (!db.open(config)) {
        std::string j = "{\"ok\":false,\"error\":\"config_error\",\"hint\":\"" + db.lastError() + "\"}";
        render(j, op, target, "nxfire", false, -1);
        return 1;
    }
    const std::string pHost = stripScheme(db.firebaseUrl());

    Result r = db.execute(req);

    Json out = Json::object();
    out.set("ok", Json(r.ok));
    if (r.ok) {
        out.set("data", r.data);
        out.set("meta", Json::object());
        out["meta"].set("latency_ms", Json(int64_t(db.lastLatencyMs())));
    } else {
        out.set("error", Json(r.error));
        if (!r.hint.empty()) out.set("hint", Json(r.hint));
        out.set("http_status", Json(int64_t(r.httpStatus)));
    }

    render(out.dump(), op, target, pHost, r.ok, db.lastLatencyMs());
    return r.ok ? 0 : 1;
}
