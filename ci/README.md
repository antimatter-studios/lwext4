# CI environments

Every CI job runs inside a container defined in this directory, so the whole
test chain can be reproduced on any machine with only docker installed —
nothing else needs to be set up on the host.

```sh
ci/run.sh --list          # jobs and the environment each one uses
ci/run.sh <job>           # build the image if needed and run the job
ci/run.sh --shell <env>   # interactive shell in an environment
```

- `envs/<env>/Dockerfile` — one image per toolchain/emulator environment.
  Images are tagged with a hash of their directory and rebuilt when it
  changes. Environments should build natively on both x86_64 and arm64.
- `jobs/<job>.sh` — the job itself, run from the repository root inside the
  container. The `# env:` header selects the image; an optional
  `# platform: linux/amd64` header is only for tools without an arm64 build.

GitHub Actions workflows call `ci/run.sh <job>` and nothing else, so CI and
local runs are identical.
