"""NxfireDB SDK (Python) — klien tipis untuk server lokal NxfireDB.

Tanpa dependensi (pakai urllib bawaan). Server lokal meneruskan ke Firebase.

Contoh:
    from nxfire import Nxfire
    db = Nxfire("127.0.0.1", 9026)
    db.set("/users/budi", {"name": "Budi", "age": 30})
    print(db.get("/users/budi")["data"])
    db.push("/chat/room1", {"text": "halo"})
    db.remove("/users/budi")

Juga bisa dipakai dari CLI:
    python nxfire.py '{"op":"get","path":"/users"}' --port 9026
"""

import json
import sys
import urllib.error
import urllib.request


class NxfireError(Exception):
    pass


class Nxfire:
    def __init__(self, host="127.0.0.1", port=9026, timeout=30):
        self.base = "http://%s:%d/" % (host, int(port))
        self.timeout = timeout

    # --- inti ---
    def call(self, op, path=None, value=None, query=None):
        req = {"op": op}
        if path is not None:
            req["path"] = path
        if value is not None:
            req["value"] = value
        if query is not None:
            req["query"] = query
        data = json.dumps(req).encode("utf-8")
        r = urllib.request.Request(
            self.base, data=data, headers={"Content-Type": "application/json"}
        )
        try:
            with urllib.request.urlopen(r, timeout=self.timeout) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            # Server membalas JSON walau status 4xx/5xx.
            body = e.read().decode("utf-8")
            try:
                return json.loads(body)
            except ValueError:
                raise NxfireError("HTTP %d: %s" % (e.code, body))
        except urllib.error.URLError as e:
            raise NxfireError("tidak bisa menghubungi server: %s" % e.reason)

    # --- operasi ---
    def set(self, path, value):
        return self.call("set", path, value)

    def get(self, path, query=None):
        return self.call("get", path, query=query)

    def update(self, path, value):
        return self.call("update", path, value)

    def push(self, path, value):
        return self.call("push", path, value)

    def remove(self, path):
        return self.call("remove", path)

    # --- bantu ---
    def value(self, path, query=None):
        """Kembalikan `data` saja (atau None bila gagal)."""
        res = self.get(path, query)
        return res.get("data") if res.get("ok") else None


def _main(argv):
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    op_json = None
    host, port = "127.0.0.1", 9026
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--host":
            i += 1
            host = argv[i]
        elif a == "--port":
            i += 1
            port = int(argv[i])
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            op_json = a
        i += 1

    if not op_json:
        print(__doc__)
        return 2

    req = json.loads(op_json)
    db = Nxfire(host, port)
    res = db.call(
        req.get("op"), req.get("path"), req.get("value"), req.get("query")
    )
    print(json.dumps(res, ensure_ascii=False))
    return 0 if res.get("ok") else 1


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
