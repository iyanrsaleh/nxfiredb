<?php
/**
 * NxfireDB SDK (PHP) — klien tipis untuk server lokal NxfireDB.
 *
 * Contoh:
 *   $db = new Nxfire('127.0.0.1', 9026);
 *   $db->set('/users/budi', ['name' => 'Budi', 'age' => 30]);
 *   $r = $db->get('/users/budi');
 *   var_dump($r['data']);
 *
 * Tanpa dependensi luar (pakai curl bila ada, fallback stream).
 */

class Nxfire
{
    private $base;

    public function __construct($host = '127.0.0.1', $port = 9026, $timeout = 30)
    {
        $this->base = "http://{$host}:{$port}/";
        $this->timeout = $timeout;
    }

    public function call($op, $path = null, $value = null, $query = null)
    {
        $req = ['op' => $op];
        if ($path !== null)  $req['path']  = $path;
        if ($value !== null) $req['value'] = $value;
        if ($query !== null) $req['query'] = $query;

        $body = json_encode($req);

        if (function_exists('curl_init')) {
            $ch = curl_init($this->base);
            curl_setopt_array($ch, [
                CURLOPT_POST           => true,
                CURLOPT_POSTFIELDS     => $body,
                CURLOPT_HTTPHEADER     => ['Content-Type: application/json'],
                CURLOPT_RETURNTRANSFER => true,
                CURLOPT_TIMEOUT        => $this->timeout,
            ]);
            $raw = curl_exec($ch);
            curl_close($ch);
        } else {
            $ctx = stream_context_create([
                'http' => [
                    'method'        => 'POST',
                    'header'        => "Content-Type: application/json\r\n",
                    'content'       => $body,
                    'ignore_errors' => true,
                    'timeout'       => $this->timeout,
                ],
            ]);
            $raw = file_get_contents($this->base, false, $ctx);
        }

        return json_decode($raw, true);
    }

    public function set($path, $value)    { return $this->call('set', $path, $value); }
    public function get($path, $query = null) { return $this->call('get', $path, null, $query); }
    public function update($path, $value) { return $this->call('update', $path, $value); }
    public function push($path, $value)   { return $this->call('push', $path, $value); }
    public function remove($path)         { return $this->call('remove', $path); }
}