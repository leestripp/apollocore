#!/usr/bin/env bash
# scripts/uninstall.sh — remove the ApolloCore MCP service.
#
# What this does:
#   1. Stops + disables the systemd unit.
#   2. Removes the unit file + env file from /etc/.
#   3. Removes the runtime directories under /var/lib /var/log /var/cache.
#   4. Optionally removes /opt/apollocore/ (controlled by --purge).
#   5. Optionally removes the `apollocore` system user (controlled by --purge-user).
#
# Re-running is safe.
#
# Usage:
#   sudo ./scripts/uninstall.sh                # leave /opt/apollocore + user alone
#   sudo ./scripts/uninstall.sh --purge        # also delete /opt/apollocore
#   sudo ./scripts/uninstall.sh --purge-user   # also delete the system user
#   APOLLO_KEEP_DATA=1 sudo ./scripts/uninstall.sh --purge  # keep /var/lib data
#
# Exit codes:
#   0  removed (or nothing to remove)
#   1  not root / no sudo
#   2  systemd unit wasn't installed (nothing to do)

set -euo pipefail

PURGE=0
PURGE_USER=0
for arg in "$@"; do
    case "$arg" in
        --purge)        PURGE=1 ;;
        --purge-user)   PURGE_USER=1 ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "uninstall.sh: unknown arg $arg" >&2; exit 1 ;;
    esac
done

if [[ $EUID -ne 0 ]] && ! sudo -n true 2>/dev/null; then
    echo "uninstall.sh: needs sudo" >&2; exit 1
fi
run() {
    if [[ $EUID -eq 0 ]]; then "$@"
    else sudo "$@"
    fi
}

: "${APOLLO_INSTALL_PREFIX:=/opt/apollocore}"
: "${APOLLO_DATA_DIR:=/var/lib/apollocore/data}"

# ─────────────────────────── systemd ───────────────────────────
if [[ -f /etc/systemd/system/apollo_mcp_server.service ]]; then
    echo "uninstall.sh: stopping + disabling apollo_mcp_server.service"
    run systemctl stop    apollo_mcp_server.service 2>/dev/null || true
    run systemctl disable apollo_mcp_server.service 2>/dev/null || true
    run rm -f /etc/systemd/system/apollo_mcp_server.service
    run systemctl daemon-reload
else
    echo "uninstall.sh: no systemd unit installed; skipping"
fi

# ─────────────────────────── /etc config ───────────────────────
run rm -f /etc/default/apollocore

# ─────────────────────────── runtime dirs ─────────────────────
if [[ -z "${APOLLO_KEEP_DATA:-}" ]]; then
    echo "uninstall.sh: removing $APOLLO_DATA_DIR, /var/log/apollocore, /var/cache/apollocore"
    run rm -rf /var/log/apollocore /var/cache/apollocore
    run rm -rf "$APOLLO_DATA_DIR"
fi

# ─────────────────────────── /opt ──────────────────────────────
if [[ "$PURGE" == "1" ]]; then
    echo "uninstall.sh: removing $APOLLO_INSTALL_PREFIX"
    run rm -rf "$APOLLO_INSTALL_PREFIX"
fi

# ─────────────────────────── user ─────────────────────────────
if [[ "$PURGE_USER" == "1" ]]; then
    if id apollocore >/dev/null 2>&1; then
        echo "uninstall.sh: deleting apollocore system user"
        run userdel apollocore || true
    fi
fi

echo "uninstall.sh: ✔ done"
[[ "$PURGE" == "0" ]] && echo "  (pass --purge to also remove $APOLLO_INSTALL_PREFIX)"
[[ "$PURGE_USER" == "0" ]] && echo "  (pass --purge-user to also remove the apollocore user)"