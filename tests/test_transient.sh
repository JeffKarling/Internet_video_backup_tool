#!/usr/bin/env bash
# test_transient.sh
# End-to-end integration test runner for vb_generator using systemd transient services.

set -euo pipefail

# Define workspace and test binary locations (absolute paths for systemd execution)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TEST_BIN="/usr/sbin/vb_generator_test_bin"

# Text styling
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0;m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[FAIL]${NC} $1" >&2
}

# 1. Verify Root/Sudo privileges
if [ "$EUID" -ne 0 ]; then
    log_error "This script must be run as root (or via sudo)."
    exit 1
fi

# 2. Cleanup function to reset system state
cleanup() {
    log_info "Cleaning up test_transient files and services..."
    
    # Stop and disable systemd units if active
    systemctl stop yt-dlp@test_transient.path yt-dlp@test_transient.timer yt-dlp-vpn@test_transient.service 2>/dev/null || true
    systemctl disable yt-dlp@test_transient.path yt-dlp@test_transient.timer yt-dlp-vpn@test_transient.service 2>/dev/null || true
    
    # Remove files
    rm -f /etc/wireguard/test_transient.conf
    rm -f /etc/yt-dlp-vpn/test_transient.resolv
    rm -f /var/lib/yt-dlp/conf/test_transient.conf
    rm -f /var/lib/yt-dlp/conf/test_transient.batch.txt
    rm -f /var/lib/yt-dlp/conf/test_transient.history.txt
    rm -rf /var/lib/yt-dlp/videos/test_transient
    
    # Remove archived copies
    rm -f /var/lib/yt-dlp/conf/deleted_profiles/test_transient.conf
    rm -f /var/lib/yt-dlp/conf/deleted_profiles/test_transient.batch.txt
    rm -f /var/lib/yt-dlp/conf/deleted_profiles/test_transient.history.txt
    rm -f /var/lib/yt-dlp/conf/deleted_profiles/test_transient.wg.conf
    rm -f /var/lib/yt-dlp/conf/deleted_profiles/test_transient.resolv
    
    # Remove temporary JSON configuration files
    rm -f "${WORKSPACE_DIR}/test_placeholder.json"
    rm -f "${WORKSPACE_DIR}/test_profile.json"
    rm -f "${TEST_BIN}"
    
    # Reset transient units from systemd
    systemctl reset-failed 2>/dev/null || true
    systemctl daemon-reload
}

# Register cleanup on exit
trap cleanup EXIT

# Run initial cleanup to be safe
cleanup

log_info "Compiling vb_generator locally to ${TEST_BIN}..."
g++ -O3 -std=c++17 -o "${TEST_BIN}" vb_generator/vb_generator.cpp -lsystemd

# ==============================================================================
# TEST CASE 1: Template Safety Rejection (Failed Path)
# ==============================================================================
log_info "Running Test Case 1: Template Safety Rejection..."

# Reset status and run transient service
systemctl reset-failed test-vb-template-fail 2>/dev/null || true
exit_code=0
systemd-run --wait --unit=test-vb-template-fail --property=Type=notify \
  "${TEST_BIN}" "${WORKSPACE_DIR}/templates/profiles-template.json" >/dev/null 2>&1 || exit_code=$?

if [ "$exit_code" -ne 22 ]; then
    log_error "Test Case 1 failed: Expected exit code 22, got $exit_code"
    exit 1
fi

status_output=$(systemctl status test-vb-template-fail 2>&1 || true)
journal_output=$(journalctl -u test-vb-template-fail --no-pager -n 20 2>&1)

if [[ ! "$status_output" =~ "Error: Template path is blocked" ]]; then
    log_error "Test Case 1 failed: systemctl status does not reflect 'Error: Template path is blocked'"
    echo "$status_output"
    exit 1
fi

if [[ ! "$journal_output" =~ "Error: Releasing profiles using the default template JSON direct path is blocked" ]]; then
    log_error "Test Case 1 failed: journalctl does not contain expected error log"
    echo "$journal_output"
    exit 1
fi

log_success "Test Case 1 passed: Template direct path correctly blocked with status status/error reflections."

# ==============================================================================
# TEST CASE 2: Placeholder Content Validation (Failed Path)
# ==============================================================================
log_info "Running Test Case 2: Placeholder Content Validation..."

cat << 'EOF' > "${WORKSPACE_DIR}/test_placeholder.json"
{
  "profiles": [
    {
      "name": "profileA",
      "wireguard": {
        "private_key": "yAuD..."
      }
    }
  ]
}
EOF

systemctl reset-failed test-vb-placeholder-fail 2>/dev/null || true
exit_code=0
systemd-run --wait --unit=test-vb-placeholder-fail --property=Type=notify \
  "${TEST_BIN}" "${WORKSPACE_DIR}/test_placeholder.json" >/dev/null 2>&1 || exit_code=$?

if [ "$exit_code" -ne 22 ]; then
    log_error "Test Case 2 failed: Expected exit code 22, got $exit_code"
    exit 1
fi

status_output=$(systemctl status test-vb-placeholder-fail 2>&1 || true)
journal_output=$(journalctl -u test-vb-placeholder-fail --no-pager -n 20 2>&1)

if [[ ! "$status_output" =~ "Error: Placeholder keys detected" ]]; then
    log_error "Test Case 2 failed: systemctl status does not reflect 'Error: Placeholder keys detected'"
    echo "$status_output"
    exit 1
fi

if [[ ! "$journal_output" =~ "Error: JSON config file contains placeholder keys" ]]; then
    log_error "Test Case 2 failed: journalctl does not contain expected placeholder error log"
    echo "$journal_output"
    exit 1
fi

log_success "Test Case 2 passed: Placeholder keys correctly blocked with status/error reflections."

# ==============================================================================
# TEST CASE 3: Transient Service Creation (Happy Path)
# ==============================================================================
log_info "Running Test Case 3: Transient Service Creation (Happy Path)..."

cat << 'EOF' > "${WORKSPACE_DIR}/test_profile.json"
{
  "profiles": [
    {
      "name": "test_transient",
      "wireguard": {
        "private_key": "aBcD1234aBcD1234aBcD1234aBcD1234aBcD1234aBcD=",
        "address": "10.4.0.2/32",
        "public_key": "xYzD5678xYzD5678xYzD5678xYzD5678xYzD5678xYzD=",
        "endpoint": "192.168.1.100:51820",
        "dns": "8.8.8.8, 1.1.1.1"
      }
    }
  ]
}
EOF

systemctl reset-failed test-vb-generator 2>/dev/null || true
systemd-run --wait --unit=test-vb-generator --property=Type=notify \
  "${TEST_BIN}" "${WORKSPACE_DIR}/test_profile.json"

# Assert files are created with correct permissions
check_permission() {
    local path=$1
    local expected=$2
    if [ ! -e "$path" ]; then
        log_error "File/Folder does not exist: $path"
        exit 1
    fi
    local actual
    actual=$(stat -c "%a" "$path")
    if [ "$actual" != "$expected" ]; then
        log_error "Permission mismatch for $path: Expected $expected, got $actual"
        exit 1
    fi
}

check_permission "/etc/wireguard/test_transient.conf" "600"
check_permission "/etc/yt-dlp-vpn/test_transient.resolv" "644"
check_permission "/var/lib/yt-dlp/conf/test_transient.conf" "666"
check_permission "/var/lib/yt-dlp/conf/test_transient.batch.txt" "666"
check_permission "/var/lib/yt-dlp/conf/test_transient.history.txt" "666"
check_permission "/var/lib/yt-dlp/videos/test_transient" "777"

# Verify systemctl states
if ! systemctl is-enabled yt-dlp@test_transient.path >/dev/null 2>&1; then
    log_error "yt-dlp@test_transient.path is not enabled"
    exit 1
fi

if ! systemctl is-active yt-dlp@test_transient.path >/dev/null 2>&1; then
    log_error "yt-dlp@test_transient.path is not active"
    exit 1
fi

if ! systemctl is-enabled yt-dlp@test_transient.timer >/dev/null 2>&1; then
    log_error "yt-dlp@test_transient.timer is not enabled"
    exit 1
fi

# Verify systemd-run journal outputs (transient units are GC'd upon exit, so we verify using journal logs)
journal_output=$(journalctl -u test-vb-generator --no-pager -n 50 2>&1)
if [[ ! "$journal_output" =~ "Profile test_transient services successfully activated" ]]; then
    log_error "Test Case 3 failed: journal does not contain per-profile successful activation message"
    echo "$journal_output"
    exit 1
fi

if [[ ! "$journal_output" =~ "Profile generator operation completed successfully." ]]; then
    log_error "Test Case 3 failed: journal does not contain final operations success message"
    echo "$journal_output"
    exit 1
fi

log_success "Test Case 3 passed: Normal generation succeeded, permissions are correct, units activated, and status messages verified."

# ==============================================================================
# TEST CASE 4: Healing Mode (--fix)
# ==============================================================================
log_info "Running Test Case 4: Healing Mode (--fix)..."

# Forcefully disable and stop unit
systemctl stop yt-dlp@test_transient.path
systemctl disable yt-dlp@test_transient.path

systemctl reset-failed test-vb-generator-fix 2>/dev/null || true
systemd-run --wait --unit=test-vb-generator-fix --property=Type=notify \
  "${TEST_BIN}" --fix "${WORKSPACE_DIR}/test_profile.json"

# Verify units are enabled and active again
if ! systemctl is-enabled yt-dlp@test_transient.path >/dev/null 2>&1; then
    log_error "Healing failed: yt-dlp@test_transient.path is not enabled after --fix"
    exit 1
fi

if ! systemctl is-active yt-dlp@test_transient.path >/dev/null 2>&1; then
    log_error "Healing failed: yt-dlp@test_transient.path is not active after --fix"
    exit 1
fi

# Verify systemd-run journal outputs for fix mode
journal_output=$(journalctl -u test-vb-generator-fix --no-pager -n 50 2>&1)
if [[ ! "$journal_output" =~ "Profile test_transient services successfully activated" ]]; then
    log_error "Test Case 4 failed: journal does not contain per-profile successful activation message"
    echo "$journal_output"
    exit 1
fi

if [[ ! "$journal_output" =~ "Profile generator operation completed successfully." ]]; then
    log_error "Test Case 4 failed: journal does not contain final operations success message"
    echo "$journal_output"
    exit 1
fi

log_success "Test Case 4 passed: Healing mode restored disabled units and reported correct status messages."

# ==============================================================================
# TEST CASE 5: Archival Deletion Mode (--delete)
# ==============================================================================
log_info "Running Test Case 5: Archival Deletion Mode (--delete)..."

systemctl reset-failed test-vb-generator-delete 2>/dev/null || true
systemd-run --wait --unit=test-vb-generator-delete --property=Type=notify \
  "${TEST_BIN}" --delete test_transient

# Verify files are archived to deleted_profiles/
check_existence() {
    local path=$1
    local expect_exists=$2
    if [ "$expect_exists" = "true" ]; then
        if [ ! -f "$path" ]; then
            log_error "Archival verification failed: File should exist but doesn't: $path"
            exit 1
        fi
    else
        if [ -f "$path" ]; then
            log_error "Archival verification failed: File should not exist but does: $path"
            exit 1
        fi
    fi
}

check_existence "/etc/wireguard/test_transient.conf" "false"
check_existence "/etc/yt-dlp-vpn/test_transient.resolv" "false"
check_existence "/var/lib/yt-dlp/conf/test_transient.conf" "false"

# Check deleted_profiles content
check_existence "/var/lib/yt-dlp/conf/deleted_profiles/test_transient.conf" "true"
check_existence "/var/lib/yt-dlp/conf/deleted_profiles/test_transient.batch.txt" "true"
check_existence "/var/lib/yt-dlp/conf/deleted_profiles/test_transient.history.txt" "true"
check_existence "/var/lib/yt-dlp/conf/deleted_profiles/test_transient.wg.conf" "true"
check_existence "/var/lib/yt-dlp/conf/deleted_profiles/test_transient.resolv" "true"

# Verify units are inactive and disabled
if systemctl is-enabled yt-dlp@test_transient.path >/dev/null 2>&1; then
    log_error "Deletion failed: yt-dlp@test_transient.path is still enabled"
    exit 1
fi

if systemctl is-active yt-dlp@test_transient.path >/dev/null 2>&1; then
    log_error "Deletion failed: yt-dlp@test_transient.path is still active"
    exit 1
fi

# Verify status output for delete mode in journal
journal_output=$(journalctl -u test-vb-generator-delete --no-pager -n 50 2>&1)
if [[ ! "$journal_output" =~ "Profile test_transient services disabled and stopped" ]]; then
    log_error "Test Case 5 failed: journal does not contain deletion success message"
    echo "$journal_output"
    exit 1
fi

log_success "Test Case 5 passed: Deletion successfully disabled/stopped services, archived files, and reported status."

# ==============================================================================
# SUMMARY
# ==============================================================================
echo -e "\n${GREEN}--------------------------------------------------${NC}"
echo -e "${GREEN}ALL INTEGRATION TESTS PASSED SUCCESSFULLY!${NC}"
echo -e "${GREEN}--------------------------------------------------${NC}"
