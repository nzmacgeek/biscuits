#!/usr/bin/env bash
set -euo pipefail

PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="${1:-$PWD/musl-blueyos}"
CLONE_URL="https://github.com/nzmacgeek/musl-blueyos.git"
FORCE=0

if [[ ${2:-} == "--force" ]]; then
  FORCE=1
fi

echo "Patch dir: $PATCH_DIR"
echo "Target musl repo: $REPO_DIR"

# Ensure patches exist
PATCHES=("$PATCH_DIR/musl-getrlimit.git.patch" "$PATCH_DIR/musl-setrlimit.git.patch")
for p in "${PATCHES[@]}"; do
  if [[ ! -f "$p" ]]; then
    echo "ERROR: patch not found: $p"
    exit 1
  fi
done

# Clone if missing
if [[ ! -d "$REPO_DIR/.git" ]]; then
  echo "Cloning musl repo into $REPO_DIR..."
  git clone "$CLONE_URL" "$REPO_DIR"
fi

pushd "$REPO_DIR" > /dev/null

# Ensure clean working tree unless forced
if [[ $FORCE -ne 1 ]]; then
  if [[ -n "$(git status --porcelain)" ]]; then
    echo "ERROR: repository has uncommitted changes. Clean or pass --force to continue." >&2
    git status --porcelain
    popd > /dev/null
    exit 1
  fi
fi

# Apply patches in order
for p in "${PATCHES[@]}"; do
  printf "\nApplying patch: %s\n" "$(basename "$p")"
  # Try git apply first (plain unified diff), commit if it applies.
  if git apply --index "$p"; then
    git add -A
    git commit -m "Apply patch: $(basename "$p")"
    echo "git apply + commit ok"
    continue
  fi

  # Fallback to git am (mailbox-format patch)
  if git am "$p"; then
    echo "git am ok"
    continue
  fi

  echo "Both git apply and git am failed for $p" >&2
  git am --abort >/dev/null 2>&1 || true
  popd > /dev/null
  exit 1
done

  printf "\nAll patches applied successfully to %s\n" "$REPO_DIR"

popd > /dev/null
