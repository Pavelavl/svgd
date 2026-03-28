/**
 * Authentication module for SVGD Metrics Dashboard
 * Handles JWT token-based authentication
 */

const TOKEN_KEY = 'jwt_token';

function setToken(token) {
    localStorage.setItem(TOKEN_KEY, token);
}

function getToken() {
    return localStorage.getItem(TOKEN_KEY);
}

function clearToken() {
    localStorage.removeItem(TOKEN_KEY);
}

/**
 * Logout: clear token and redirect to login page
 */
function logout() {
    clearToken();
    window.location.href = '/login.html';
}

/**
 * Login with password to get JWT token
 * @param {string} password
 * @returns {Promise<boolean>} True if login successful
 */
async function login(password) {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 10000);

    try {
        const response = await fetch('/_auth/login', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ password }),
            signal: controller.signal
        });

        clearTimeout(timeoutId);

        const data = await response.json();

        if (!response.ok) {
            throw new Error(data.error || 'Login failed');
        }

        if (!data.token) {
            throw new Error('No token received from server');
        }

        setToken(data.token);
        return true;
    } catch (error) {
        clearTimeout(timeoutId);
        if (error.name === 'AbortError') {
            throw new Error('Login timeout - server did not respond');
        }
        throw error;
    }
}

/**
 * Check if user is authenticated (token exists)
 * @returns {boolean}
 */
function isAuthenticated() {
    return !!getToken();
}

/**
 * Make an authenticated API request
 * Adds Bearer token, handles 401 by redirecting to login
 * @param {string} url
 * @param {RequestInit} options
 * @returns {Promise<Response>}
 */
async function fetchWithAuth(url, options = {}) {
    const token = getToken();

    if (!token) {
        window.location.href = '/login.html';
        throw new Error('No authentication token');
    }

    const headers = {
        ...options.headers,
        'Authorization': `Bearer ${token}`
    };

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 10000);

    try {
        const response = await fetch(url, { ...options, headers, signal: controller.signal });

        if (response.status === 401) {
            clearToken();
            window.location.href = '/login.html';
            throw new Error('Authentication failed');
        }

        return response;
    } finally {
        clearTimeout(timeoutId);
    }
}

/**
 * Check authentication and redirect to login if not authenticated.
 * Call this on pages that require authentication.
 */
function checkAuth() {
    if (!isAuthenticated()) {
        window.location.href = '/login.html';
    }
}
