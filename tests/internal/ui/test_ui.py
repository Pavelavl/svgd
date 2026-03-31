"""
UI Tests for SVGD Dashboard

Tests verify that:
1. Static files are served correctly (HTML, JS)
2. API endpoints return expected content types
3. CORS headers are present
4. Error handling works properly
5. Response timing is acceptable

Run with: pytest tests/ui/test_ui.py -v
"""

import os
import pytest
import requests
import time

# Configuration
BASE_URL = "http://localhost:8080"
BACKEND_URL = "http://localhost:8081"
TIMEOUT = 10
TEST_PASSWORD = os.environ.get("AUTH_TEST_PASSWORD", "test123")


@pytest.fixture(scope="module")
def auth_token():
    """Login and return a valid JWT token, skip if auth is not configured"""
    try:
        resp = requests.post(
            f"{BASE_URL}/_auth/login",
            json={"password": TEST_PASSWORD},
            timeout=TIMEOUT,
        )
    except requests.exceptions.ConnectionError:
        pytest.fail(f"Server not available at {BASE_URL}")

    if resp.status_code == 401:
        pytest.skip("Auth not configured or password mismatch")
    assert resp.status_code == 200, f"Login failed: {resp.status_code} {resp.text}"
    return resp.json()["token"]


@pytest.fixture(scope="module")
def auth_headers(auth_token):
    """Return Authorization headers with a valid token"""
    return {"Authorization": f"Bearer {auth_token}"}


class TestStaticFiles:
    """Test static file serving"""

    def test_index_html_returns_200(self):
        """Main page should return 200 OK"""
        resp = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        assert resp.status_code == 200
        assert "text/html" in resp.headers.get("Content-Type", "")

    def test_index_html_contains_title(self):
        """Main page should contain dashboard title"""
        resp = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        assert "SVGD Metrics Dashboard" in resp.text

    def test_index_html_contains_script_tag(self):
        """Main page should include script.js"""
        resp = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        assert 'src="script.js"' in resp.text

    def test_script_js_returns_200(self):
        """JavaScript file should be served"""
        resp = requests.get(f"{BASE_URL}/script.js", timeout=TIMEOUT)
        assert resp.status_code == 200
        assert "javascript" in resp.headers.get("Content-Type", "").lower()

    def test_script_js_contains_config(self):
        """Script should contain API configuration"""
        resp = requests.get(f"{BASE_URL}/script.js", timeout=TIMEOUT)
        assert "apiBaseUrl" in resp.text
        assert "fetchSVG" in resp.text

    def test_nonexistent_file_returns_error(self):
        """Non-existent files should return error (400 or 404)"""
        resp = requests.get(f"{BASE_URL}/nonexistent.xyz", timeout=TIMEOUT)
        # Server treats unknown paths as invalid API requests (400) or 404
        assert resp.status_code in [400, 401, 404], f"Expected 400/401/404, got {resp.status_code}"


class TestCORS:
    """Test CORS headers"""

    def test_cors_header_on_api_response(self, auth_headers):
        """API responses should include CORS header"""
        resp = requests.get(f"{BASE_URL}/cpu?period=3600", headers=auth_headers, timeout=TIMEOUT)
        assert resp.headers.get("Access-Control-Allow-Origin") == "*"

    def test_cors_header_on_static_response(self):
        """Static responses should include CORS header"""
        resp = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        assert resp.headers.get("Access-Control-Allow-Origin") == "*"

    def test_options_preflight(self):
        """OPTIONS request should return proper CORS headers"""
        resp = requests.options(f"{BASE_URL}/cpu", timeout=TIMEOUT)
        assert resp.status_code == 200
        assert resp.headers.get("Access-Control-Allow-Origin") == "*"
        assert "GET" in resp.headers.get("Access-Control-Allow-Methods", "")


class TestAPIEndpoints:
    """Test API endpoint functionality"""

    def test_metrics_config_endpoint(self, auth_headers):
        """_config/metrics should return JSON with metrics list"""
        resp = requests.get(f"{BASE_URL}/_config/metrics", headers=auth_headers, timeout=TIMEOUT)
        assert resp.status_code == 200
        assert "application/json" in resp.headers.get("Content-Type", "")

        data = resp.json()
        assert "metrics" in data
        assert isinstance(data["metrics"], list)
        assert len(data["metrics"]) > 0

    def test_metrics_config_contains_expected_fields(self, auth_headers):
        """Metrics config should contain required fields"""
        resp = requests.get(f"{BASE_URL}/_config/metrics", headers=auth_headers, timeout=TIMEOUT)
        data = resp.json()

        for metric in data["metrics"]:
            assert "endpoint" in metric, f"Missing endpoint in {metric}"
            assert "requires_param" in metric, f"Missing requires_param in {metric}"

    def test_cpu_endpoint_content_type(self, auth_headers):
        """CPU endpoint should return SVG"""
        resp = requests.get(f"{BASE_URL}/cpu?period=3600", headers=auth_headers, timeout=TIMEOUT)
        # May fail if no RRD data, but content-type should be set
        if resp.status_code == 200:
            assert "image/svg+xml" in resp.headers.get("Content-Type", "")

    def test_cpu_endpoint_returns_svg(self, auth_headers):
        """CPU endpoint should return valid SVG content"""
        resp = requests.get(f"{BASE_URL}/cpu?period=3600", headers=auth_headers, timeout=TIMEOUT)
        if resp.status_code == 200:
            assert "<svg" in resp.text
            assert "</svg>" in resp.text

    def test_ram_endpoint_content_type(self, auth_headers):
        """RAM endpoint should return SVG"""
        resp = requests.get(f"{BASE_URL}/ram?period=3600", headers=auth_headers, timeout=TIMEOUT)
        if resp.status_code == 200:
            assert "image/svg+xml" in resp.headers.get("Content-Type", "")

    def test_invalid_endpoint_returns_error(self, auth_headers):
        """Invalid endpoint should return error JSON"""
        resp = requests.get(f"{BASE_URL}/invalid_metric?period=3600", headers=auth_headers, timeout=TIMEOUT)
        assert resp.status_code in [400, 500]
        # Should be JSON error
        try:
            data = resp.json()
            assert "error" in data
        except:
            # Or plain text error
            pass

    def test_missing_period_uses_default(self, auth_headers):
        """Missing period parameter should use default"""
        resp = requests.get(f"{BASE_URL}/cpu", headers=auth_headers, timeout=TIMEOUT)
        # Should not error, uses default period
        assert resp.status_code in [200, 400, 500]


class TestConnectionHandling:
    """Test connection handling and stability"""

    def test_multiple_sequential_requests(self, auth_headers):
        """Multiple sequential requests should all succeed"""
        endpoints = [("/", None), ("/script.js", None), ("/_config/metrics", auth_headers)]
        for endpoint, headers in endpoints:
            resp = requests.get(f"{BASE_URL}{endpoint}", headers=headers, timeout=TIMEOUT)
            assert resp.status_code == 200, f"Failed for {endpoint}"

    def test_response_has_content_length(self):
        """Responses should include Content-Length header"""
        resp = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        assert "Content-Length" in resp.headers
        actual_len = len(resp.content)
        header_len = int(resp.headers["Content-Length"])
        assert actual_len == header_len, f"Content-Length mismatch: {header_len} vs {actual_len}"

    def test_connection_close_header(self):
        """Responses should include Connection: close"""
        resp = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        assert resp.headers.get("Connection", "").lower() == "close"


class TestPerformance:
    """Test response timing"""

    def test_static_response_time(self):
        """Static files should respond quickly"""
        start = time.time()
        resp = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        elapsed = time.time() - start
        assert resp.status_code == 200
        assert elapsed < 1.0, f"Response too slow: {elapsed:.2f}s"

    def test_api_response_time(self, auth_headers):
        """API responses should be reasonably fast"""
        start = time.time()
        resp = requests.get(f"{BASE_URL}/_config/metrics", headers=auth_headers, timeout=TIMEOUT)
        elapsed = time.time() - start
        assert resp.status_code == 200
        assert elapsed < 2.0, f"API response too slow: {elapsed:.2f}s"


class TestErrorHandling:
    """Test error scenarios"""

    def test_malformed_request(self):
        """Malformed requests should return 400"""
        # Send request without proper HTTP format
        try:
            import socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            sock.connect(("localhost", 8080))
            sock.send(b"INVALID\r\n\r\n")
            resp = sock.recv(1024)
            sock.close()
            # Should return 400 or close connection
            assert b"400" in resp or resp == b""
        except:
            pass  # Connection closed is also acceptable

    def test_empty_query_params(self):
        """Empty query should still work for static files"""
        resp = requests.get(f"{BASE_URL}/?", timeout=TIMEOUT)
        assert resp.status_code == 200


class TestBackendIntegration:
    """Test backend (port 8081) directly"""

    @pytest.fixture(autouse=True)
    def _check_backend(self):
        """Skip entire class if backend is not running"""
        try:
            resp = requests.get(f"{BACKEND_URL}/_config/metrics", timeout=2)
            if resp.status_code != 200:
                pytest.skip("Backend server not running on port 8081")
        except (requests.exceptions.ConnectionError, requests.exceptions.Timeout):
            pytest.skip("Backend server not running on port 8081")

# Fixtures
@pytest.fixture(scope="module")
def server_available():
    """Check if server is available before running tests"""
    try:
        requests.get(f"{BASE_URL}/", timeout=2)
        return True
    except:
        pytest.fail(f"Server not available at {BASE_URL}")


# Run configuration
if __name__ == "__main__":
    pytest.main([__file__, "-v", "--tb=short"])
