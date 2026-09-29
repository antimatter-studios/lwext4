# SPDX-License-Identifier: BSD-3-Clause
# env: base
# Rebuild the integration branch from ci/integration/branches.txt (merge
# conflicts fail the job). See ci/integration/rebuild.sh.
#
#   ci/run.sh integration-rebuild [--push | --check] [remote]
#
# --push needs push credentials inside the container; run
# ci/integration/rebuild.sh --push on the host to use your own.
sh ci/integration/rebuild.sh "$@"
