#!/usr/bin/env bash
set -euo pipefail

workspace=""

cleanup() {
  if [[ -n "${workspace:-}" && "$workspace" == /tmp/linux-command-practice.* && -d "$workspace" ]]; then
    rm -rf -- "$workspace"
  fi
}

trap cleanup EXIT

run() {
  printf '\n$'
  printf ' %q' "$@"
  printf '\n'
  "$@"
}

run_may_timeout() {
  printf '\n$'
  printf ' %q' "$@"
  printf '\n'
  "$@" || true
}

run_shell() {
  printf '\n$ %s\n' "$*"
  bash -c "$*"
}

note() {
  printf '\n# %s\n' "$*"
}

usage() {
  cat <<'EOF'
Usage:
  scripts/linux_commands_demo.sh

This creates a temporary /tmp/linux-command-practice.* directory, demonstrates
safe versions of common Linux commands, and removes the temporary directory on
exit. It does not modify project files and does not run sudo chown.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ $# -ne 0 ]]; then
  usage >&2
  exit 2
fi

workspace="$(mktemp -d /tmp/linux-command-practice.XXXXXX)"
note "Temporary workspace: $workspace"

cd "$workspace"

note "Create practice files and folders"
run_shell "printf 'hello\nsecond line\nlast line\n' > file1"
run mkdir -p folder1
run_shell "printf 'text inside a file\n' > folder1/file.txt"
run_shell "printf 'service started\n' > file.log"

note "Copy files and folders"
run cp file1 file2
run cp -r folder1 folder2

note "Move or rename"
run mv file2 renamed-file

note "Read files"
run head file1
run tail file1
if command -v less >/dev/null 2>&1; then
  run less --quit-if-one-screen folder1/file.txt
else
  note "Skipping less demo because less is not installed"
fi

note "Follow logs briefly"
run_shell "printf 'new log line\n' >> file.log"
if command -v timeout >/dev/null 2>&1; then
  run_may_timeout timeout 1 tail -f file.log
else
  note "Skipping tail -f demo because the timeout command is not available"
fi

note "Find files and folders"
run find . -name file.txt
run find . -type f
run find . -type d

note "Search text"
run grep text folder1/file.txt
run grep -r text .
run grep -rn text .

note "Change permissions"
run_shell "printf '#!/usr/bin/env bash\nprintf \"demo script ran\\\\n\"\n' > script.sh"
run chmod +x script.sh
run ./script.sh
run chmod 755 script.sh
run chmod 644 folder1/file.txt

note "Delete only temporary practice files"
run rm renamed-file
run rm -r folder2
run mkdir disposable-build-output
run rm -rf disposable-build-output

note "Ownership command is shown but not executed"
printf '$ sudo chown -R "$USER:$USER" "%s/folder1"\n' "$workspace"
printf 'Skipped: sudo changes ownership as administrator. Read docs/linux-commands.md first.\n'

note "Demo complete. Temporary workspace will be removed automatically."
