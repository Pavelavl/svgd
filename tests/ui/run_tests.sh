#!/bin/bash
# Run UI tests for SVGD Dashboard
#
# Prerequisites:
#   1. Start servers: ./bin/svgd (port 8081) and ./bin/svgd-gate (port 8080)
#
# Usage:
#   ./run_tests.sh          # Run all tests
#   ./run_tests.sh -v       # Verbose output
#   ./run_tests.sh -k CORS  # Run only CORS tests

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venv"

# Check if servers are running
check_servers() {
    echo "Checking servers..."

    if curl -s --connect-timeout 2 "http://localhost:8080/" > /dev/null 2>&1; then
        echo "✓ Gate server (8080) is running"
    else
        echo "✗ Gate server (8080) is NOT running"
        echo "  Start with: ./bin/svgd-gate"
        return 1
    fi

    # Check backend via gate (backend uses LSRP, not HTTP)
    if curl -s --connect-timeout 2 "http://localhost:8080/_config/metrics" > /dev/null 2>&1; then
        echo "✓ Backend (8081) is accessible via gate"
    else
        echo "⚠ Backend may not be responding (gate is running but API fails)"
        echo "  Start with: ./bin/svgd"
        # Not a hard error - tests will show actual failures
    fi
}

# Setup virtual environment and install dependencies
setup_venv() {
    if [ ! -d "$VENV_DIR" ]; then
        echo "Creating virtual environment..."
        python3 -m venv "$VENV_DIR"
    fi

    # Activate and install deps
    source "$VENV_DIR/bin/activate"

    if ! python3 -c "import requests; import pytest" 2>/dev/null; then
        echo "Installing dependencies..."
        pip install -q -r "$SCRIPT_DIR/requirements.txt"
    fi
}

main() {
    echo "=== SVGD UI Tests ==="
    echo ""

    # Check servers
    check_servers || exit 1
    echo ""

    # Setup venv
    setup_venv

    # Run tests
    echo ""
    echo "Running tests..."
    echo "---"
    cd "$SCRIPT_DIR"
    python3 -m pytest test_ui.py "$@"
}

main "$@"
