#!/bin/bash
set -e

PROFILE="$1"
CONF="/etc/wireguard/${PROFILE}.conf"
NS="netns_${PROFILE}"

if [ -z "$PROFILE" ]; then
    echo "Usage: $0 <profile> [up|down]"
    exit 1
fi

ACTION="${2:-up}"

if [ "$ACTION" = "up" ]; then
    echo "Starting VPN namespace setup for profile: ${PROFILE}..."
    
    # 1. Clean up any historical remnants of this profile
    ip netns del "$NS" 2>/dev/null || true
    ip link del "$PROFILE" 2>/dev/null || true

    # 2. Create the network namespace
    ip netns add "$NS"
    echo "Created network namespace: ${NS}"

    # 3. Create the WireGuard interface on the host (anchoring socket in default namespace)
    ip link add "$PROFILE" type wireguard
    echo "Created WireGuard interface '${PROFILE}' in default namespace"

    # 4. Move the WireGuard interface to the isolated namespace
    ip link set "$PROFILE" netns "$NS"
    echo "Moved interface '${PROFILE}' to namespace '${NS}'"

    # 5. Configure WireGuard settings inside the namespace using nsenter
    # Filter out wg-quick specific options (Address, MTU, DNS, Table, Pre/Post scripts) which cause wg setconf to crash
    grep -vE '^[[:space:]]*(Address|MTU|DNS|Table|PostUp|PostDown|PreUp|PreDown)' "$CONF" | /usr/bin/nsenter --net=/run/netns/"$NS" wg setconf "$PROFILE" /dev/stdin
    echo "Applied WireGuard configuration to '${PROFILE}' inside namespace"

    # 6. Parse and apply IP addresses from the WireGuard config file
    addresses=$(grep -i '^[[:space:]]*Address' "$CONF" | cut -d= -f2- | cut -d# -f1 | cut -d';' -f1 | tr -d ' ' | tr ',' ' ')
    if [ -z "$addresses" ]; then
        echo "ERROR: No Address found in ${CONF}"
        exit 1
    fi
    
    for addr in $addresses; do
        /usr/bin/nsenter --net=/run/netns/"$NS" ip address add "$addr" dev "$PROFILE"
        echo "Configured address ${addr} on interface '${PROFILE}' inside namespace"
    done

    # 7. Parse and apply MTU if present, default to 1420
    mtu=$(grep -i '^[[:space:]]*MTU' "$CONF" | cut -d= -f2- | cut -d# -f1 | cut -d';' -f1 | tr -d ' ')
    if [ -z "$mtu" ]; then
        mtu=1420
    fi
    /usr/bin/nsenter --net=/run/netns/"$NS" ip link set mtu "$mtu" up dev "$PROFILE"
    echo "Set link '${PROFILE}' UP with MTU ${mtu}"

    # 8. Set up routing inside the network namespace
    has_ipv4=false
    has_ipv6=false
    for addr in $addresses; do
        if [[ "$addr" =~ : ]]; then
            has_ipv6=true
        else
            has_ipv4=true
        fi
    done

    if [ "$has_ipv4" = true ]; then
        /usr/bin/nsenter --net=/run/netns/"$NS" ip route add default dev "$PROFILE"
        echo "Configured default IPv4 route through interface '${PROFILE}'"
    fi
    if [ "$has_ipv6" = true ]; then
        /usr/bin/nsenter --net=/run/netns/"$NS" ip -6 route add default dev "$PROFILE"
        echo "Configured default IPv6 route through interface '${PROFILE}'"
    fi
    
    echo "VPN namespace setup for profile ${PROFILE} completed successfully."

elif [ "$ACTION" = "down" ]; then
    echo "Tearing down VPN namespace for profile: ${PROFILE}..."
    ip netns del "$NS" 2>/dev/null || true
    ip link del "$PROFILE" 2>/dev/null || true
    echo "Cleanup completed."
fi
