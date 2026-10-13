// Package nxfire — NxfireDB SDK (Go). Klien tipis untuk server lokal NxfireDB.
//
// Contoh:
//
//	c := nxfire.New("127.0.0.1", 9026)
//	c.Set("/users/budi", map[string]any{"name": "Budi", "age": 30})
//	r, _ := c.Get("/users/budi")
//	fmt.Println(string(r.Data))
package nxfire

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

type Client struct {
	Base string
	HTTP *http.Client
}

// Result adalah balasan server.
type Result struct {
	Ok    bool            `json:"ok"`
	Data  json.RawMessage `json:"data,omitempty"`
	Error string          `json:"error,omitempty"`
	Hint  string          `json:"hint,omitempty"`
}

func New(host string, port int) *Client {
	return &Client{
		Base: fmt.Sprintf("http://%s:%d/", host, port),
		HTTP: &http.Client{Timeout: 30 * time.Second},
	}
}

func (c *Client) call(req map[string]any) (*Result, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return nil, err
	}
	resp, err := c.HTTP.Post(c.Base, "application/json", bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	var r Result
	if err := json.NewDecoder(resp.Body).Decode(&r); err != nil {
		return nil, err
	}
	return &r, nil
}

func (c *Client) Set(path string, value any) (*Result, error) {
	return c.call(map[string]any{"op": "set", "path": path, "value": value})
}

func (c *Client) Get(path string, query map[string]any) (*Result, error) {
	req := map[string]any{"op": "get", "path": path}
	if query != nil {
		req["query"] = query
	}
	return c.call(req)
}

func (c *Client) Update(path string, value any) (*Result, error) {
	return c.call(map[string]any{"op": "update", "path": path, "value": value})
}

func (c *Client) Push(path string, value any) (*Result, error) {
	return c.call(map[string]any{"op": "push", "path": path, "value": value})
}

func (c *Client) Remove(path string) (*Result, error) {
	return c.call(map[string]any{"op": "remove", "path": path})
}
