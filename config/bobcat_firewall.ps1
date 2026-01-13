# Bobcat Engine Firewall Rules - Relaxed Version
# Run as Administrator

# Allow all traffic on the equipment subnets
New-NetFirewallRule -DisplayName "Bobcat - Subnet 192.168.0.x" -Direction Inbound -Protocol TCP -RemoteAddress 192.168.0.0/24 -Action Allow
New-NetFirewallRule -DisplayName "Bobcat - Subnet 192.168.0.x Out" -Direction Outbound -Protocol TCP -RemoteAddress 192.168.0.0/24 -Action Allow

New-NetFirewallRule -DisplayName "Bobcat - Subnet 192.168.2.x" -Direction Inbound -Protocol TCP -RemoteAddress 192.168.2.0/24 -Action Allow
New-NetFirewallRule -DisplayName "Bobcat - Subnet 192.168.2.x Out" -Direction Outbound -Protocol TCP -RemoteAddress 192.168.2.0/24 -Action Allow

New-NetFirewallRule -DisplayName "Bobcat - Subnet 192.168.3.x" -Direction Inbound -Protocol TCP -RemoteAddress 192.168.3.0/24 -Action Allow
New-NetFirewallRule -DisplayName "Bobcat - Subnet 192.168.3.x Out" -Direction Outbound -Protocol TCP -RemoteAddress 192.168.3.0/24 -Action Allow

New-NetFirewallRule -DisplayName "Bobcat - Subnet 192.168.4.x" -Direction Inbound -Protocol TCP -RemoteAddress 192.168.4.0/24 -Action Allow
New-NetFirewallRule -DisplayName "Bobcat - Subnet 192.168.4.x Out" -Direction Outbound -Protocol TCP -RemoteAddress 192.168.4.0/24 -Action Allow

# Also allow UDP for GigE cameras (discovery)
New-NetFirewallRule -DisplayName "Bobcat - GigE Camera UDP" -Direction Inbound -Protocol UDP -RemoteAddress 192.168.2.0/24,192.168.4.0/24 -Action Allow
New-NetFirewallRule -DisplayName "Bobcat - GigE Camera UDP Out" -Direction Outbound -Protocol UDP -RemoteAddress 192.168.2.0/24,192.168.4.0/24 -Action Allow

Write-Host "Relaxed firewall rules created successfully!" -ForegroundColor Green
