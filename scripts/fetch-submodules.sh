#!/usr/bin/env bash
# Fetch submodules in CI. vault-a and vault-b are private, so they come in
# over SSH with a read-only deploy key each (VAULT_A_KEY / VAULT_B_KEY
# secrets). Deploy keys don't expire, unlike the old VAULT_TOKEN PAT.
set -euo pipefail

: "${VAULT_A_KEY:?VAULT_A_KEY secret is not set}"
: "${VAULT_B_KEY:?VAULT_B_KEY secret is not set}"

mkdir -p ~/.ssh
chmod 700 ~/.ssh
printf '%s\n' "$VAULT_A_KEY" > ~/.ssh/vault_a
printf '%s\n' "$VAULT_B_KEY" > ~/.ssh/vault_b
chmod 600 ~/.ssh/vault_a ~/.ssh/vault_b

# GitHub's published host key (gh api meta).
echo "github.com ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl" >> ~/.ssh/known_hosts

# One host alias per key, since GitHub only accepts a deploy key for its own repo.
cat >> ~/.ssh/config <<'EOF'
Host vault-a
  HostName github.com
  User git
  IdentityFile ~/.ssh/vault_a
  IdentitiesOnly yes

Host vault-b
  HostName github.com
  User git
  IdentityFile ~/.ssh/vault_b
  IdentitiesOnly yes
EOF

git config --global url."vault-a:dtentiion/vault-a.git".insteadOf "https://github.com/dtentiion/vault-a.git"
git config --global url."vault-b:dtentiion/vault-b.git".insteadOf "https://github.com/dtentiion/vault-b.git"

git submodule sync --recursive
git submodule update --init --force --recursive --depth=1
