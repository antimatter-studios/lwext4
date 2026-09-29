# env: smoke
# Checks that ci/run.sh can build an environment and run a job in it.
test -f /src/ci/run.sh
echo "runner ok: $(uname -m), uid $(id -u)"
