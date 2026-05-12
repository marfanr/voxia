#!/usr/bin/env bash
set -e

EXT_IF=$(ip route | awk '/default/ {print $5; exit}')

sudo sysctl -w net.ipv4.ip_forward=1

# TAP
sudo ip tuntap add dev tap0 mode tap 2>/dev/null || true
sudo ip addr add 192.168.100.1/24 dev tap0 2>/dev/null || true
sudo ip link set tap0 up

# NAT hanya ke interface internet
sudo iptables -t nat -A POSTROUTING -o $EXT_IF -j MASQUERADE

# Forward
sudo iptables -A FORWARD -i tap0 -o $EXT_IF -j ACCEPT
sudo iptables -A FORWARD -i $EXT_IF -o tap0 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT