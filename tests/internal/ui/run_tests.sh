#!/bin/bash
# Run UI tests for SVGD Dashboard
#
# Prerequisites:
#   1. Start servers: ./bin/svgd (port 8081) and ./bin/svgd-gate (port 8080)
#
# Usage:
#   ./run_tests.sh              # Run API tests only (fast)
#   ./run_tests.sh --browser    # Run all tests including browser (Playwright)
#   ./run_tests.sh -v           # Verbose output
#   ./run_tests.sh -k CORS      # Run only CORS tests

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venv"

# Check if servers are running
check_servers() {
    echo "Checking servers..."

    if curl -s --connect-timeout 2 "http://localhost:8080/" > /dev/null 2>&1; then
        echo "  Gate server (8080) is running"
    else
        echo "  Gate server (8080) is NOT running"
        echo "  Start with: ./bin/svgd-gate"
        return 1
    fi

    # Check backend via gate (backend uses LSRP, not HTTP)
    if curl -s --connect-timeout 2 "http://localhost:8080/_config/metrics" > /dev/null 2>&1; then
        echo "  Backend (8081) is accessible via gate"
    else
        echo "  Backend may not be responding (gate is running but API fails)"
        echo "  Start with: ./bin/svgd"
    fi
}

# Setup virtual environment and install dependencies
setup_venv() {
    if [ ! -d "$VENV_DIR" ]; then
        echo "Creating virtual environment..."
        python3 -m venv "$VENV_DIR"
    fi

    source "$VENV_DIR/bin/activate"

    if ! python3 -c "import requests; import pytest" 2>/dev/null; then
        echo "Installing dependencies..."
        pip install -q -i https://pypi.org/simple/ -r "$SCRIPT_DIR/requirements.txt" || \
        pip install -q -i https://mirrors.aliyun.com/pypi/simple/ -r "$SCRIPT_DIR/requirements.txt"
    fi
}

# Install playwright browsers if needed
setup_playwright() {
    if ! python3 -c "from playwright.sync_api import sync_playwright" 2>/dev/null; then
        echo "Installing Playwright..."
        pip install -q pytest-playwright
    fi
    if ! python3 -m playwright install --dry-run 2>&1 | grep -q "already installed"; then
        echo "Installing Playwright browsers..."
        python3 -m playwright install chromium
    fi
}

main() {
    echo "=== SVGD UI Tests ==="
    echo ""

    check_servers || exit 1
    echo ""

    setup_venv

    cd "$SCRIPT_DIR"

    BROWSER_MODE=false
    TEST_ARGS=()
    for arg in "$@"; do
        case "$arg" in
            --browser) BROWSER_MODE=true ;;
            *) TEST_ARGS+=("$arg") ;;
        esac
    done

    if [ "$BROWSER_MODE" = true ]; then
        echo "Running all tests (API + Browser)..."
        echo "---"
        setup_playwright
        python3 -m pytest test_ui.py test_auth.py test_auth_browser.py "${TEST_ARGS[@]}"
    else
        echo "Running API tests..."
        echo "---"
        python3 -m pytest test_ui.py test_auth.py "${TEST_ARGS[@]}"
    fi
}

main "$@"
