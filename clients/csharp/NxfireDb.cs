// NxfireDB SDK (C# / .NET 6+) — klien tipis untuk server lokal NxfireDB.
//
// Contoh:
//   var db = new Nxfire("127.0.0.1", 9026);
//   await db.SetAsync("/users/budi", new { name = "Budi", age = 30 });
//   var r = await db.GetAsync("/users/budi");
//   Console.WriteLine(r.Data);
//
// Tanpa dependensi luar (System.Net.Http + System.Text.Json bawaan).

using System;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace NxfireDB
{
    public sealed class NxfireResult
    {
        public bool Ok { get; init; }
        public JsonElement? Data { get; init; }
        public string? Error { get; init; }
        public string? Hint { get; init; }
    }

    public sealed class Nxfire
    {
        private readonly HttpClient _http;

        public Nxfire(string host = "127.0.0.1", int port = 9026, int timeoutSeconds = 30)
        {
            _http = new HttpClient
            {
                BaseAddress = new Uri($"http://{host}:{port}/"),
                Timeout = TimeSpan.FromSeconds(timeoutSeconds),
            };
        }

        private async Task<NxfireResult> CallAsync(object request)
        {
            var json = JsonSerializer.Serialize(request);
            using var content = new StringContent(json, Encoding.UTF8, "application/json");
            using var resp = await _http.PostAsync("", content);
            var body = await resp.Content.ReadAsStringAsync();

            using var doc = JsonDocument.Parse(body);
            var root = doc.RootElement;
            return new NxfireResult
            {
                Ok = root.TryGetProperty("ok", out var ok) && ok.GetBoolean(),
                Data = root.TryGetProperty("data", out var data) ? data.Clone() : null,
                Error = root.TryGetProperty("error", out var err) ? err.GetString() : null,
                Hint = root.TryGetProperty("hint", out var hint) ? hint.GetString() : null,
            };
        }

        public Task<NxfireResult> SetAsync(string path, object value) =>
            CallAsync(new { op = "set", path, value });

        public Task<NxfireResult> GetAsync(string path, object? query = null) =>
            CallAsync(query is null
                ? new { op = "get", path }
                : (object)new { op = "get", path, query });

        public Task<NxfireResult> UpdateAsync(string path, object value) =>
            CallAsync(new { op = "update", path, value });

        public Task<NxfireResult> PushAsync(string path, object value) =>
            CallAsync(new { op = "push", path, value });

        public Task<NxfireResult> RemoveAsync(string path) =>
            CallAsync(new { op = "remove", path });
    }
}
