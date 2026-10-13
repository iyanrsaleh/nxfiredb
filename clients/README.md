# Klien Resmi NxfireDB (SDK)

NxfireDB berbicara **HTTP + JSON polos** dengan server lokal, jadi klien
tersedia untuk banyak bahasa **tanpa driver khusus**. Semua klien
menghubungi server lokal NxfireDB (`http://127.0.0.1:9026/`), yang lalu
meneruskan ke Firebase Realtime Database.

## Pola dasar (sama di semua bahasa)

| Bahasa | Berkas | Uji di sini |
|---|---|---|
| Python | `python/nxfire.py` | ✅ |
| JavaScript (Node) | `javascript/nxfire.js` | ✅ |
| C# (.NET 6+) | `csharp/NxfireDb.cs` | ✅ (kompilasi) |
| Go | `go/nxfire.go` | (butuh toolchain Go) |
| PHP | `php/nxfire.php` | (butuh PHP) |

Operasi: `set`, `get`, `update`, `push`, `remove` (+ `query`).

## Contoh

**Python**
```python
from nxfire import Nxfire
db = Nxfire("127.0.0.1", 9026)
db.set("/users/budi", {"name": "Budi", "age": 30})
print(db.get("/users/budi")["data"])
db.push("/chat/room1", {"text": "halo"})
db.remove("/users/budi")
```

**JavaScript (Node)**
```js
const { Nxfire } = require('./nxfire');
const db = new Nxfire('127.0.0.1', 9026);
await db.set('/users/budi', { name: 'Budi', age: 30 });
console.log(await db.get('/users/budi'));
```

**C#**
```csharp
var db = new Nxfire("127.0.0.1", 9026);
await db.SetAsync("/users/budi", new { name = "Budi", age = 30 });
var r = await db.GetAsync("/users/budi");
```

**Go**
```go
c := nxfire.New("127.0.0.1", 9026)
c.Set("/users/budi", map[string]any{"name": "Budi", "age": 30})
r, _ := c.Get("/users/budi", nil)
```

**PHP**
```php
$db = new Nxfire('127.0.0.1', 9026);
$db->set('/users/budi', ['name' => 'Budi', 'age' => 30]);
$r = $db->get('/users/budi');
```

## Bahasa lain

Cukup kirim `POST` JSON ke server lokal:

```
POST http://127.0.0.1:9026/
Content-Type: application/json

{ "op": "set", "path": "/users/budi", "value": { "name": "Budi" } }
```

Tidak perlu menunggu SDK resmi.

## Query

```json
{ "op": "get", "path": "/users",
  "query": { "orderBy": "age", "limitToLast": 10 } }
```

Parameter: `orderBy` (`$key`/`$value`/nama child), `startAt`, `endAt`,
`equalTo`, `limitToFirst`, `limitToLast`, `shallow`.
