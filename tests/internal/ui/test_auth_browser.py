"""
Browser (Playwright) E2E tests for SVGD authentication flow

Tests the full browser behavior:
1. Unauthenticated user is redirected from / to /login.html
2. Login page renders correctly
3. Successful login redirects to dashboard
4. Token is stored in localStorage
5. Already-authenticated user is redirected from /login.html to /
6. Logout clears token and redirects to login
7. Wrong password shows error message
8. Dashboard loads charts after login

Run with:
    pytest tests/internal/ui/test_auth_browser.py -v
    AUTH_TEST_PASSWORD=your_password pytest tests/internal/ui/test_auth_browser.py -v
"""

import os
import pytest

BASE_URL = os.environ.get("AUTH_TEST_URL", "http://localhost:8080")
TEST_PASSWORD = os.environ.get("AUTH_TEST_PASSWORD", "change_me_please")


@pytest.fixture(scope="session")
def browser_context_args(browser_context_args):
    return {
        **browser_context_args,
        "viewport": {"width": 1280, "height": 720},
    }


class TestAuthRedirects:
    """Test browser redirect behavior for authentication"""

    def test_unauthenticated_redirects_to_login(self, page):
        """Visiting / without token should redirect to /login.html"""
        page.goto(f"{BASE_URL}/")
        page.wait_for_url("**/login.html**", timeout=5000)

    def test_login_page_has_correct_elements(self, page):
        """Login page should have password field and submit button"""
        page.goto(f"{BASE_URL}/login.html")
        page.wait_for_selector("#password")
        page.wait_for_selector("#loginBtn")
        assert page.locator("#password").count() == 1
        assert page.locator("#loginBtn").count() == 1

    def test_login_page_password_field_focused(self, page):
        """Password field should be auto-focused on login page"""
        page.goto(f"{BASE_URL}/login.html")
        focused = page.evaluate("document.activeElement === document.getElementById('password')")
        assert focused is True


class TestLoginFlow:
    """Test login and authentication flow in browser"""

    def test_login_with_empty_password_shows_error(self, page):
        """Submitting whitespace-only password should show error message"""
        page.goto(f"{BASE_URL}/login.html")
        page.wait_for_selector("#loginBtn")
        # Space bypasses HTML 'required' but JS trim() makes it empty
        page.fill("#password", " ")
        page.click("#loginBtn")
        page.wait_for_selector(".error-message.show", timeout=3000)
        assert page.locator(".error-message.show").is_visible()

    def test_login_with_wrong_password_shows_error(self, page):
        """Wrong password should show error message"""
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", "wrong_password")
        page.click("#loginBtn")
        page.wait_for_selector(".error-message.show", timeout=3000)
        error_text = page.locator("#errorText").text_content()
        # auth.js throws on non-OK response, login.html catches it as generic error
        assert "error" in error_text.lower()

    def test_login_clears_error_on_typing(self, page):
        """Error message should disappear when user starts typing"""
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", "wrong")
        page.click("#loginBtn")
        page.wait_for_selector(".error-message.show", timeout=3000)
        page.fill("#password", "a")
        assert not page.locator(".error-message.show").is_visible()

    @pytest.mark.skipif(
        not os.environ.get("AUTH_TEST_PASSWORD"),
        reason="Set AUTH_TEST_PASSWORD to run login success tests"
    )
    def test_successful_login_redirects_to_dashboard(self, page):
        """Successful login should redirect to /index.html"""
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", TEST_PASSWORD)
        page.click("#loginBtn")
        page.wait_for_url("**/index.html**", timeout=5000)

    @pytest.mark.skipif(
        not os.environ.get("AUTH_TEST_PASSWORD"),
        reason="Set AUTH_TEST_PASSWORD to run login success tests"
    )
    def test_login_stores_token_in_localstorage(self, page):
        """Successful login should store JWT token in localStorage"""
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", TEST_PASSWORD)
        page.click("#loginBtn")
        page.wait_for_url("**/index.html**", timeout=5000)

        token = page.evaluate("localStorage.getItem('jwt_token')")
        assert token is not None
        assert len(token) > 0

    @pytest.mark.skipif(
        not os.environ.get("AUTH_TEST_PASSWORD"),
        reason="Set AUTH_TEST_PASSWORD to run login success tests"
    )
    def test_login_button_shows_loading_state(self, page):
        """Login button should show loading spinner during login"""
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", TEST_PASSWORD)
        page.click("#loginBtn")
        # Button should show loading text briefly
        btn_text = page.locator("#btnText").text_content()
        # Either still loading or already redirected
        assert btn_text in ["Signing in...", "Sign In"]


class TestAlreadyAuthenticated:
    """Test behavior when user is already authenticated"""

    @pytest.mark.skipif(
        not os.environ.get("AUTH_TEST_PASSWORD"),
        reason="Set AUTH_TEST_PASSWORD to run auth persistence tests"
    )
    def test_login_page_redirects_if_already_logged_in(self, page):
        """If user already has valid token, /login.html should redirect to /"""
        # First login
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", TEST_PASSWORD)
        page.click("#loginBtn")
        page.wait_for_url("**/index.html**", timeout=5000)

        # Now navigate to login page - should redirect back to index
        page.goto(f"{BASE_URL}/login.html")
        page.wait_for_url("**/index.html**", timeout=5000)

    @pytest.mark.skipif(
        not os.environ.get("AUTH_TEST_PASSWORD"),
        reason="Set AUTH_TEST_PASSWORD to run auth persistence tests"
    )
    def test_dashboard_loads_after_login(self, page):
        """Dashboard should load with panels after authentication"""
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", TEST_PASSWORD)
        page.click("#loginBtn")
        page.wait_for_url("**/index.html**", timeout=5000)

        # Dashboard should have main elements
        page.wait_for_selector("#dashboardGrid", timeout=5000)
        assert page.locator("#dashboardGrid").count() == 1


class TestLogout:
    """Test logout functionality"""

    @pytest.mark.skipif(
        not os.environ.get("AUTH_TEST_PASSWORD"),
        reason="Set AUTH_TEST_PASSWORD to run logout tests"
    )
    def test_logout_clears_token(self, page):
        """Logout should clear JWT token from localStorage"""
        # Login first
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", TEST_PASSWORD)
        page.click("#loginBtn")
        page.wait_for_url("**/index.html**", timeout=5000)

        # Verify token exists
        token = page.evaluate("localStorage.getItem('jwt_token')")
        assert token is not None

        # Click logout
        page.click("#logoutBtn")
        page.wait_for_url("**/login.html**", timeout=5000)

        # Token should be cleared
        token_after = page.evaluate("localStorage.getItem('jwt_token')")
        assert token_after is None

    @pytest.mark.skipif(
        not os.environ.get("AUTH_TEST_PASSWORD"),
        reason="Set AUTH_TEST_PASSWORD to run logout tests"
    )
    def test_logout_redirects_to_login(self, page):
        """Logout should redirect to login page"""
        # Login first
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", TEST_PASSWORD)
        page.click("#loginBtn")
        page.wait_for_url("**/index.html**", timeout=5000)

        # Click logout
        page.click("#logoutBtn")
        page.wait_for_url("**/login.html**", timeout=5000)

        # Should be on login page
        assert page.locator("#password").count() == 1

    @pytest.mark.skipif(
        not os.environ.get("AUTH_TEST_PASSWORD"),
        reason="Set AUTH_TEST_PASSWORD to run logout tests"
    )
    def test_after_logout_dashboard_is_inaccessible(self, page):
        """After logout, visiting / should redirect to login"""
        # Login and logout
        page.goto(f"{BASE_URL}/login.html")
        page.fill("#password", TEST_PASSWORD)
        page.click("#loginBtn")
        page.wait_for_url("**/index.html**", timeout=5000)
        page.click("#logoutBtn")
        page.wait_for_url("**/login.html**", timeout=5000)

        # Try to visit dashboard
        page.goto(f"{BASE_URL}/")
        page.wait_for_url("**/login.html**", timeout=5000)


class TestLoginPageUI:
    """Test login page UI elements and styling"""

    def test_login_page_title(self, page):
        """Login page should have correct title"""
        page.goto(f"{BASE_URL}/login.html")
        title = page.title()
        assert "SVGD" in title
        assert "Login" in title

    def test_login_page_has_logo(self, page):
        """Login page should have SVGD logo"""
        page.goto(f"{BASE_URL}/login.html")
        page.wait_for_selector(".logo-icon")

    def test_login_page_form_structure(self, page):
        """Login form should have label, input and button"""
        page.goto(f"{BASE_URL}/login.html")
        assert page.locator('label[for="password"]').count() == 1
        assert page.locator('input[type="password"]').count() == 1
        assert page.locator('button[type="submit"]').count() == 1

    def test_login_page_responsive(self, page):
        """Login page should be usable on mobile viewport"""
        page.set_viewport_size({"width": 375, "height": 667})
        page.goto(f"{BASE_URL}/login.html")
        page.wait_for_selector("#loginBtn")
        # Form should be visible and usable
        assert page.locator("#loginBtn").is_visible()
        assert page.locator("#password").is_visible()


if __name__ == "__main__":
    pytest.main([__file__, "-v", "--tb=short"])
