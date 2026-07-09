# Lab 01 — Building a Professional Enterprise Linux Environment

**Module:** 01-Linux-Systems-Engineering  
**Difficulty:** Beginner  
**Estimated Time:** 90–120 minutes  
**Prerequisites:** Volume 0 — Foundation

### Learning Objectives
- Install and harden Ubuntu Server LTS for enterprise use
- Configure a reproducible development environment
- Practice documentation as code
- Understand Linux as the foundation for all later infrastructure

### Lab Environment
- Host: Windows, macOS, or Linux
- Hypervisor: VirtualBox (recommended) or VMware Workstation
- Guest OS: Ubuntu Server 24.04 LTS

### Step-by-Step Tasks

#### Part 1: VM Creation & Installation (30 min)
1. Download Ubuntu Server 24.04 LTS ISO.
2. Create VM with these specs:
   - 4 vCPUs
   - 8 GB RAM
   - 60 GB disk (dynamically allocated)
   - Network: Bridged Adapter
3. Install Ubuntu Server with these options:
   - Custom storage layout (separate /, /var, /home)
   - Enable OpenSSH server
   - Create user `aieng` with sudo privileges

#### Part 2: System Hardening & Tooling (40 min)
```bash
# Update system
sudo apt update && sudo apt full-upgrade -y

# Install essential enterprise tools
sudo apt install -y curl wget git vim htop tree unzip net-tools \
                    build-essential ca-certificates gnupg lsb-release \
                    tmux bat fzf ripgrep

# Configure useful aliases
cat << EOF >> ~/.bash_aliases
alias ll='ls -la --color=auto'
alias update='sudo apt update && sudo apt full-upgrade -y'
alias ports='sudo ss -tuln'
alias myip='curl ifconfig.me'
EOF

source ~/.bash_aliases
