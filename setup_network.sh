#!/bin/bash
#!/usr/bin/env bash
set -e

# === cleanup dulu ===

# hapus interface kalau sudah ada
if ip link show tap0 > /dev/null 2>&1; then
    sudo ip link set tap0 down || true
    sudo ip tuntap del dev tap0 mode tap || true
fi

# hapus rule iptables kalau sudah ada
sudo iptables -t nat -D POSTROUTING -o eth0 -j MASQUERADE 2>/dev/null || true
sudo iptables -D FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || true
sudo iptables -D FORWARD -i tap0 -o eth0 -j ACCEPT 2>/dev/null || true

# === setup ulang ===
sudo ip tuntap add dev tap0 mode tap
sudo ip link set tap0 up
sudo ip addr add 192.168.100.1/24 dev tap0

sudo iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
sudo iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
sudo iptables -A FORWARD -i tap0 -o eth0 -j ACCEPT

sudo iptables -t nat -A PREROUTING -p tcp --dport 8080 -j DNAT --to 192.168.100.2:80
sudo iptables -t nat -A POSTROUTING -j MASQUERADE
sudo iptables -A FORWARD -p tcp -d 192.168.100.80 --dport 80 -j ACCEPT
sudo iptables -A FORWARD -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT