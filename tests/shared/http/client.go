package http

import (
	"fmt"
	"io"
	"net/http"
	"time"
)

// Client represents an HTTP client for sending requests to the server.
type Client struct {
	host   string
	port   int
	client *http.Client
}

// Response represents the server's response.
type Response struct {
	Status byte   // 0 for success, 1 for error
	Data   []byte // Response body (SVG or error message)
}

// NewClient creates a new HTTP client.
func NewClient(host string, port int) (*Client, error) {
	return &Client{
		host:   host,
		port:   port,
		client: &http.Client{Timeout: 5 * time.Second},
	}, nil
}

// Send sends an HTTP GET request to the server and returns the response.
func (c *Client) Send(params string) (*Response, error) {
	url := fmt.Sprintf("http://%s:%d/%s", c.host, c.port, params)
	resp, err := c.client.Get(url)
	if err != nil {
		return nil, fmt.Errorf("failed to send request to %s: %v", url, err)
	}
	defer resp.Body.Close()

	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("failed to read response from %s: %v", url, err)
	}

	response := &Response{}
	if resp.StatusCode == 200 && resp.Header.Get("Content-Type") == "image/svg+xml" {
		response.Status = 0
		response.Data = data
	} else {
		response.Status = 1
		response.Data = data // Assume JSON error message
	}
	return response, nil
}

// Close is a no-op for HTTP client (kept for interface consistency).
func (c *Client) Close() error {
	return nil
}
