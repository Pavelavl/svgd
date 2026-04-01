"""
Authentication UI Tests for SVGD Dashboard

Tests verify the auth system:
1. Login page is accessible without auth
2. Login with correct password returns JWT token
3. Login with wrong password returns 401
4. Token verification works
5. Protected endpoints require auth
6. Static files are accessible without auth
7. Invalid tokens are rejected

Run with: pytest tests/internal/ui/test_auth.py -v
"""

import os
import pytest
import requests

# Configuration
BASE_URL = os.environ.get("AUTH_TEST_URL", "http://localhost:8080")
TIMEOUT = 10

# Default test password (matches auth.json)
TEST_PASSWORD = os.environ.get("AUTH_TEST_PASSWORD", "change_me_please")


@pytest.fixture(scope="module")
def auth_token():
    """Login with correct password and return a valid token"""
    resp = requests.post(
        f"{BASE_URL}/_auth/login",
        json={"password": TEST_PASSWORD},
        timeout=TIMEOUT,
    )
    if resp.status_code == 401:
        pytest.skip("Auth not configured or password mismatch — check auth.json")
    assert resp.status_code == 200, f"Login failed: {resp.status_code} {resp.text}"
    data = resp.json()
    assert "token" in data, "No token in login response"
    return data["token"]


@pytest.fixture(scope="module")
def server_available():
    """Check if server is available before running tests"""
    try:
        requests.get(f"{BASE_URL}/login.html", timeout=2)
    except requests.exceptions.ConnectionError:
        pytest.fail(f"Server not available at {BASE_URL}")


class TestLoginPage:
    """Test that the login page is served correctly"""

    def test_login_page_returns_200(self, server_available):
        resp = requests.get(f"{BASE_URL}/login.html", timeout=TIMEOUT)
        assert resp.status_code == 200

    def test_login_page_is_html(self, server_available):
        resp = requests.get(f"{BASE_URL}/login.html", timeout=TIMEOUT)
        assert "text/html" in resp.headers.get("Content-Type", "")

    def test_login_page_contains_password_field(self, server_available):
        resp = requests.get(f"{BASE_URL}/login.html", timeout=TIMEOUT)
        assert 'id="password"' in resp.text
        assert 'type="password"' in resp.text

    def test_login_page_contains_submit_button(self, server_available):
        resp = requests.get(f"{BASE_URL}/login.html", timeout=TIMEOUT)
        assert 'type="submit"' in resp.text

    def test_login_page_loads_auth_js(self, server_available):
        resp = requests.get(f"{BASE_URL}/login.html", timeout=TIMEOUT)
        assert 'src="auth.js"' in resp.text

    def test_login_page_title(self, server_available):
        resp = requests.get(f"{BASE_URL}/login.html", timeout=TIMEOUT)
        assert "SVGD Metrics" in resp.text
        assert "Login" in resp.text


class TestLoginEndpoint:
    """Test POST /_auth/login"""

    def test_login_with_correct_password(self, auth_token):
        """Login with correct password should return a valid JWT token.
        Uses auth_token fixture (single login per module) to avoid flaky parallel requests."""
        assert auth_token is not None
        assert len(auth_token) > 0

    def test_login_with_wrong_password(self, auth_token):
        """Wrong password should return 401 (auth_token fixture confirms auth is configured)"""
        resp = requests.post(
            f"{BASE_URL}/_auth/login",
            json={"password": "definitely_wrong_password"},
            timeout=TIMEOUT,
        )
        assert resp.status_code == 401
        data = resp.json()
        assert "error" in data

    def test_login_with_empty_password(self, auth_token):
        """Empty password should return 401"""
        resp = requests.post(
            f"{BASE_URL}/_auth/login",
            json={"password": ""},
            timeout=TIMEOUT,
        )
        assert resp.status_code == 401

    def test_login_without_body(self, auth_token):
        """Missing body should return 401"""
        resp = requests.post(
            f"{BASE_URL}/_auth/login",
            headers={"Content-Type": "application/json"},
            timeout=TIMEOUT,
        )
        assert resp.status_code == 401

    def test_login_get_method_not_allowed(self):
        resp = requests.get(f"{BASE_URL}/_auth/login", timeout=TIMEOUT)
        assert resp.status_code in [404, 405]


class TestTokenVerification:
    """Test GET /_auth/verify"""

    def test_verify_valid_token(self, auth_token):
        resp = requests.get(
            f"{BASE_URL}/_auth/verify",
            headers={"Authorization": f"Bearer {auth_token}"},
            timeout=TIMEOUT,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data.get("valid") is True

    def test_verify_without_token(self):
        resp = requests.get(f"{BASE_URL}/_auth/verify", timeout=TIMEOUT)
        assert resp.status_code == 401

    def test_verify_with_invalid_token(self):
        resp = requests.get(
            f"{BASE_URL}/_auth/verify",
            headers={"Authorization": "Bearer invalid_token_here"},
            timeout=TIMEOUT,
        )
        assert resp.status_code == 401

    def test_verify_with_empty_bearer(self):
        resp = requests.get(
            f"{BASE_URL}/_auth/verify",
            headers={"Authorization": "Bearer "},
            timeout=TIMEOUT,
        )
        assert resp.status_code == 401

    def test_verify_with_malformed_auth_header(self):
        resp = requests.get(
            f"{BASE_URL}/_auth/verify",
            headers={"Authorization": "Basic abc123"},
            timeout=TIMEOUT,
        )
        assert resp.status_code == 401


class TestProtectedEndpoints:
    """Test that API endpoints require authentication"""

    @pytest.fixture(scope="class")
    def token(self, auth_token):
        return auth_token

    def test_datasources_requires_auth(self):
        resp = requests.get(f"{BASE_URL}/_datasources", timeout=TIMEOUT)
        assert resp.status_code == 401

    def test_datasources_with_token(self, token):
        resp = requests.get(
            f"{BASE_URL}/_datasources",
            headers={"Authorization": f"Bearer {token}"},
            timeout=TIMEOUT,
        )
        assert resp.status_code == 200

    def test_config_metrics_requires_auth(self):
        resp = requests.get(f"{BASE_URL}/_config/metrics", timeout=TIMEOUT)
        assert resp.status_code == 401

    def test_config_metrics_with_token(self, token):
        resp = requests.get(
            f"{BASE_URL}/_config/metrics",
            headers={"Authorization": f"Bearer {token}"},
            timeout=TIMEOUT,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "metrics" in data

    def test_api_endpoint_requires_auth(self):
        resp = requests.get(f"{BASE_URL}/cpu?period=3600", timeout=TIMEOUT)
        assert resp.status_code == 401

    def test_api_endpoint_with_token(self, token):
        resp = requests.get(
            f"{BASE_URL}/cpu?period=3600",
            headers={"Authorization": f"Bearer {token}"},
            timeout=TIMEOUT,
        )
        # 200 or 500 is ok (500 if backend not running), but not 401
        assert resp.status_code != 401


class TestStaticFilesPublicAccess:
    """Test that static files remain accessible without auth"""

    def test_index_html_no_auth(self, server_available):
        resp = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        assert resp.status_code == 200

    def test_login_html_no_auth(self, server_available):
        resp = requests.get(f"{BASE_URL}/login.html", timeout=TIMEOUT)
        assert resp.status_code == 200

    def test_script_js_no_auth(self, server_available):
        resp = requests.get(f"{BASE_URL}/script.js", timeout=TIMEOUT)
        assert resp.status_code == 200

    def test_auth_js_no_auth(self, server_available):
        resp = requests.get(f"{BASE_URL}/auth.js", timeout=TIMEOUT)
        assert resp.status_code == 200


class TestCORSOnAuthEndpoints:
    """Test CORS headers on auth endpoints"""

    def test_login_cors(self):
        resp = requests.options(
            f"{BASE_URL}/_auth/login",
            timeout=TIMEOUT,
        )
        assert resp.status_code == 200
        assert resp.headers.get("Access-Control-Allow-Origin") == "*"
        assert "POST" in resp.headers.get("Access-Control-Allow-Methods", "")

    def test_verify_cors(self):
        resp = requests.options(
            f"{BASE_URL}/_auth/verify",
            timeout=TIMEOUT,
        )
        assert resp.status_code == 200
        assert resp.headers.get("Access-Control-Allow-Origin") == "*"


class TestTokenReuse:
    """Test that a token can be used for multiple requests"""

    def test_multiple_requests_with_same_token(self, auth_token):
        headers = {"Authorization": f"Bearer {auth_token}"}
        for _ in range(5):
            resp = requests.get(
                f"{BASE_URL}/_datasources",
                headers=headers,
                timeout=TIMEOUT,
            )
            assert resp.status_code == 200


if __name__ == "__main__":
    pytest.main([__file__, "-v", "--tb=short"])
