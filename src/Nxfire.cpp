#include <nxfire/Nxfire.h>

#include <nxfire/Http.h>

#include <chrono>
#include <fstream>
#include <sstream>

// NxfireDB — implementasi inti.
//
// Menerjemahkan operasi JSON lokal ke Firebase Realtime Database REST API:
//   set    -> PUT    <base><path>.json
//   get    -> GET    <base><path>.json
//   update -> PATCH  <base><path>.json
//   push   -> POST   <base><path>.json
//   remove -> DELETE <base><path>.json

namespace nxfire {

namespace {

// Pastikan path berawalan '/' dan tanpa '/' di ujung (kecuali root).
std::string normalizePath(const std::string& raw) {
    if (raw.empty()) return "/";
    std::string p = raw;
    if (p.front() != '/') p.insert(p.begin(), '/');
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

// Nilai untuk parameter orderBy: "$key"/"$value" apa adanya, nama child dikutip.
std::string orderByValue(const std::string& v) {
    if (!v.empty() && v.front() == '$') return v;
    return "\"" + v + "\"";
}

// Ambil QueryOpts dari field "query".
QueryOpts queryFrom(const Json& q) {
    QueryOpts o;
    if (!q.isObject()) return o;
    o.orderBy = q.at("orderBy").asString();
    if (q.contains("startAt")) o.startAt = q.at("startAt");
    if (q.contains("endAt"))   o.endAt   = q.at("endAt");
    if (q.contains("equalTo")) o.equalTo = q.at("equalTo");
    if (q.contains("limitToFirst")) o.limitToFirst = int(q.at("limitToFirst").asInt());
    if (q.contains("limitToLast"))  o.limitToLast  = int(q.at("limitToLast").asInt());
    o.shallow = q.at("shallow").asBool();
    return o;
}

std::string statusToError(int status) {
    switch (status) {
        case 400: return "bad_request";
        case 401: return "unauthorized";
        case 403: return "permission_denied";
        case 404: return "not_found";
        case 412: return "precondition_failed";
        case 429: return "too_many_requests";
        case 500: return "server_error";
        case 503: return "service_unavailable";
        default:  return "http_error_" + std::to_string(status);
    }
}

} // namespace

class Nxfire::Private {
public:
    std::string configPath = "nxfire.json";

    std::string project;
    std::string region;
    std::string apiKey;
    std::string secret;   // Database Secret (legacy) -> ?auth=<secret>
    std::string token;    // ID token / access token  -> ?auth=<token>
    std::string baseUrlOverride; // mis. http://127.0.0.1:9000 (Firebase Emulator / uji)
    int         port     = 9026;
    int         cacheTtl = 0;    // cache GET (detik); 0 = mati
    bool        offlineQueueOn = true;  // antrian tulis saat Firebase tak terjangkau

    bool        opened      = false;
    std::string lastError;
    int         lastLatency = 0;

    std::string baseUrl() const {
        if (!baseUrlOverride.empty()) return baseUrlOverride;
        if (project.empty()) return {};
        if (region.empty()) return "https://" + project + ".firebaseio.com";
        return "https://" + project + "." + region + ".firebasedatabase.app";
    }

    std::string authValue() const {
        if (!secret.empty()) return secret;
        return token; // boleh kosong (rules publik)
    }

    // Bangun URL lengkap untuk path + opsi query.
    std::string buildUrl(const std::string& path, const QueryOpts& opts) const {
        std::string p = normalizePath(path);
        std::string url = baseUrl() + (p == "/" ? "/.json" : p + ".json");

        std::vector<std::string> params;
        if (!opts.orderBy.empty())
            params.push_back("orderBy=" + http::urlEncode(orderByValue(opts.orderBy)));
        if (!opts.startAt.isNull())
            params.push_back("startAt=" + http::urlEncode(opts.startAt.dump()));
        if (!opts.endAt.isNull())
            params.push_back("endAt=" + http::urlEncode(opts.endAt.dump()));
        if (!opts.equalTo.isNull())
            params.push_back("equalTo=" + http::urlEncode(opts.equalTo.dump()));
        if (opts.limitToFirst > 0)
            params.push_back("limitToFirst=" + std::to_string(opts.limitToFirst));
        if (opts.limitToLast > 0)
            params.push_back("limitToLast=" + std::to_string(opts.limitToLast));
        if (opts.shallow)
            params.push_back("shallow=true");
        if (!authValue().empty())
            params.push_back("auth=" + http::urlEncode(authValue()));

        if (!params.empty()) {
            url += "?";
            for (size_t i = 0; i < params.size(); ++i) {
                if (i) url += "&";
                url += params[i];
            }
        }
        return url;
    }

    Result perform(const std::string& method,
                   const std::string& path,
                   const Json& body,
                   const QueryOpts& opts) {
        Result r;
        if (!opened) {
            r.error = "not_configured";
            r.hint  = "Panggil open(\"nxfire.json\") lebih dulu.";
            return r;
        }

        const std::string url = buildUrl(path, opts);
        const std::string payload = body.isNull() ? std::string() : body.dump();

        auto t0 = std::chrono::steady_clock::now();
        http::Response res = http::request(method, url, payload);
        auto t1 = std::chrono::steady_clock::now();
        lastLatency = int(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

        r.httpStatus = res.status;

        if (!res.error.empty()) {
            r.ok    = false;
            r.error = "network_error";
            r.hint  = res.error;
            return r;
        }

        if (res.status >= 200 && res.status < 300) {
            r.ok = true;
            if (!res.body.empty() && res.body != "null") {
                std::string perr;
                Json parsed = Json::parse(res.body, &perr);
                r.data = perr.empty() ? parsed : Json(res.body);
            }
            return r;
        }

        r.ok    = false;
        r.error = statusToError(res.status);
        if (!res.body.empty()) {
            std::string perr;
            Json parsed = Json::parse(res.body, &perr);
            if (perr.empty() && parsed.isObject() && parsed.contains("error"))
                r.hint = parsed.at("error").asString();
            else
                r.hint = res.body;
        }
        return r;
    }
};

Nxfire::Nxfire() : d(new Private()) {}
Nxfire::~Nxfire() = default;
Nxfire::Nxfire(Nxfire&&) noexcept = default;
Nxfire& Nxfire::operator=(Nxfire&&) noexcept = default;

bool Nxfire::open(const std::string& configPath) {
    d->configPath = configPath;

    std::ifstream in(configPath, std::ios::binary);
    if (!in) {
        d->lastError = "tidak bisa membuka konfigurasi: " + configPath;
        d->opened = false;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    std::string err;
    Json cfg = Json::parse(ss.str(), &err);
    if (!err.empty()) {
        d->lastError = "konfigurasi bukan JSON valid: " + err;
        d->opened = false;
        return false;
    }
    if (!cfg.isObject()) {
        d->lastError = "konfigurasi harus berupa objek JSON";
        d->opened = false;
        return false;
    }

    d->project = cfg.at("project").asString();
    d->region  = cfg.at("region").asString();
    d->apiKey  = cfg.at("api_key").asString();
    d->secret  = cfg.at("secret").asString();
    d->token   = cfg.at("token").asString();
    d->baseUrlOverride = cfg.at("base_url").asString();
    if (cfg.contains("port")) d->port = int(cfg.at("port").asInt(d->port));
    if (cfg.contains("cache_ttl")) d->cacheTtl = int(cfg.at("cache_ttl").asInt(d->cacheTtl));
    if (cfg.contains("offline_queue")) d->offlineQueueOn = cfg.at("offline_queue").asBool(true);

    if (d->project.empty() && d->baseUrlOverride.empty()) {
        d->lastError = "konfigurasi kurang field 'project'";
        d->opened = false;
        return false;
    }

    d->lastError.clear();
    d->opened = true;
    return true;
}

void Nxfire::close() { d->opened = false; }

bool Nxfire::isOpen() const { return d->opened; }

std::string Nxfire::lastError() const { return d->lastError; }

std::string Nxfire::firebaseUrl() const { return d->baseUrl(); }

int Nxfire::port() const { return d->port; }

int Nxfire::cacheTtl() const { return d->cacheTtl; }

bool Nxfire::offlineQueue() const { return d->offlineQueueOn; }

int Nxfire::lastLatencyMs() const { return d->lastLatency; }

Result Nxfire::set(const std::string& path, const Json& value) {
    return d->perform("PUT", path, value, QueryOpts{});
}

Result Nxfire::get(const std::string& path, const QueryOpts& opts) {
    return d->perform("GET", path, Json(), opts);
}

Result Nxfire::update(const std::string& path, const Json& value) {
    return d->perform("PATCH", path, value, QueryOpts{});
}

Result Nxfire::push(const std::string& path, const Json& value) {
    return d->perform("POST", path, value, QueryOpts{});
}

Result Nxfire::remove(const std::string& path) {
    return d->perform("DELETE", path, Json(), QueryOpts{});
}

Result Nxfire::execute(const Json& req) {
    Result r;
    if (!req.isObject()) {
        r.error = "bad_request";
        r.hint  = "body harus berupa objek JSON";
        return r;
    }

    std::string op = req.at("op").asString();
    // Alias op.
    if (op == "put" || op == "upsert") op = "set";
    else if (op == "select")          op = "get";
    else if (op == "patch")           op = "update";
    else if (op == "delete" || op == "del") op = "remove";
    // insert/add = sisip; push = push mentah Firebase.

    // Path dasar: dari "path", atau dari "table".
    std::string path = req.at("path").asString();
    if (path.empty() && req.contains("table")) {
        std::string table = req.at("table").asString();
        if (!table.empty()) path = normalizePath("/" + table);
    }
    if (path.empty()) {
        r.error = "bad_request";
        r.hint  = "perlu 'path' atau 'table'";
        return r;
    }

    // Nilai: "value" atau alias "data".
    Json value;
    if (req.contains("value"))     value = req.at("value");
    else if (req.contains("data")) value = req.at("data");

    // Kunci baris.
    std::string key = req.contains("key") ? req.at("key").asString()
                    : req.contains("id")  ? req.at("id").asString()
                                          : std::string();
    const std::string keyFrom = req.at("keyFrom").asString();
    const bool autoId         = req.at("autoId").asBool();

    if (op == "set" || op == "get" || op == "update" || op == "remove" || op == "push") {
        if (!key.empty()) path = normalizePath(path + "/" + key);
        if (op == "set")    return set(path, value);
        if (op == "get")    return get(path, queryFrom(req.at("query")));
        if (op == "update") return update(path, value);
        if (op == "remove") return remove(path);
        return push(path, value);  // push mentah (kunci acak Firebase)
    }

    if (op == "insert" || op == "add") {
        // Tentukan kunci: autoId > keyFrom > key/id > (acak).
        if (autoId) {
            Result cur = get(path);
            long long maxId = 0;
            if (cur.ok && cur.data.isObject()) {
                for (const auto& kv : cur.data.asObject()) {
                    try {
                        size_t used = 0;
                        long long n = std::stoll(kv.first, &used);
                        if (used == kv.first.size() && n > maxId) maxId = n;
                    } catch (...) { /* bukan angka */ }
                }
            }
            key = std::to_string(maxId + 1);
        } else if (!keyFrom.empty()) {
            if (!value.isObject() || !value.contains(keyFrom)) {
                r.error = "bad_request";
                r.hint  = "keyFrom '" + keyFrom + "' tidak ada di data";
                return r;
            }
            const Json& f = value.at(keyFrom);
            if (f.isString())      key = f.asString();
            else if (f.isInt())    key = std::to_string(f.asInt());
            else {
                r.error = "bad_request";
                r.hint  = "keyFrom '" + keyFrom + "' harus string/angka";
                return r;
            }
            if (key.empty()) {
                r.error = "bad_request";
                r.hint  = "keyFrom '" + keyFrom + "' kosong";
                return r;
            }
        }

        if (!key.empty()) return set(normalizePath(path + "/" + key), value);
        return push(path, value);  // tanpa kunci -> kunci acak Firebase
    }

    r.error = "unknown_op";
    r.hint  = "op tidak dikenal: " + op +
              " (pakai: set/get/update/push/remove/insert/select/delete)";
    return r;
}

} // namespace nxfire
