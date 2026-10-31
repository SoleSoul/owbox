#!/bin/sh
# Run the CI steps (.github/workflows/ci.yml) in a clean Arch Linux
# container, as GitHub does, on the tracked files with their uncommitted
# changes (new files must be added with git add). Needs docker.
#
#   tools/ci-local.sh [LOGFILE]
set -eu
top=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
log=${1:-/dev/stdout}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
rev=$(git -C "$top" stash create)
git -C "$top" archive "${rev:-HEAD}" | tar -x -C "$tmp"
# one script with the steps' commands, each in a subshell like a step
python3 - "$top/.github/workflows/ci.yml" > "$tmp/ci-steps.sh" <<'PY'
import sys, yaml
job = next(iter(yaml.safe_load(open(sys.argv[1]))["jobs"].values()))
print("set -e")
for step in job["steps"]:
    if "run" not in step:
        continue
    print("echo '::: %s'" % step["name"].replace("'", ""))
    print("(set -e; cd /src\n%s)" % step["run"])
PY
docker run --rm -v "$tmp:/src" "$(python3 -c "
import sys, yaml
job = next(iter(yaml.safe_load(open(sys.argv[1]))['jobs'].values()))
print(job['container'])" "$top/.github/workflows/ci.yml")" \
    sh -c "cp /src/ci-steps.sh /tmp; sh /tmp/ci-steps.sh; r=\$?;
           chown -R $(id -u):$(id -g) /src; exit \$r" > "$log" 2>&1
