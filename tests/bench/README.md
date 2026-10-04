Benchmark
=========

What lwext4 costs on a microcontroller, per operation: CPU, block I/O,
heap and stack, and the size of the library (fork issue #165). The
figures are in [docs/performance](../../docs/performance/README.md), on
the project site, and summarised in the README.

The workloads
-------------

[bench.c](bench.c) runs the operations an application does, one after the
other, with the library as a product would ship it (all features, debug
output and assertions off: `-DLWEXT4_CONFIG="CONFIG_DEBUG_PRINTF=0;CONFIG_DEBUG_ASSERT=0"`),
on a 3 MiB RAM disk: `ext4_mkfs` (ext4, 1 KiB blocks, a 1 MiB
journal), mounting (with journal recovery and start), creating a file,
writing 4 KiB and 1 MiB, reading 1 MiB back, 100 lookups in a directory
of 200 entries, truncating the 1 MiB file, unlinking it, and unmounting.
For each it prints

    BENCH <platform> <operation> <count> <unit> reads <n> writes <n> heap <bytes> stack <bytes>

and `PASS` at the end if every operation succeeded.

How each figure is measured
---------------------------

| Figure | How |
|---|---|
| CPU | On the Cortex-M boards: instructions. QEMU runs with `-icount shift=0,sleep=off`: its virtual clock advances one nanosecond per instruction executed, whatever the host does. The board's CMSDK APB timer counts that clock, so its ticks across an operation are an exact count, the same on every run and every host, to the resolution of one tick (40 instructions; calibrated at start with a loop of known length, [platform_mps2.c](platform_mps2.c)). Semihosting's SYS_ELAPSED is not used: QEMU answers it with the host's clock. On a PC: nanoseconds, for information only (not compared). |
| Block reads and writes | Counted by the RAM disk of the benchmark. |
| Heap | Peak of the bytes allocated during the operation: `malloc`, `calloc`, `realloc` and `free` are wrapped (`-Wl,--wrap`, [bench_heap.c](bench_heap.c)). |
| Stack | The stack is painted with a pattern before the operation and the deepest overwritten byte found after it (Cortex-M only). |
| Flash and static RAM | `arm-none-eabi-size` of `liblwext4.a`: text, data and bss. |

The platforms are QEMU's MPS2 boards: Cortex-M0 and M3 on `mps2-an385`,
M4 on `mps2-an386`, M7 on `mps2-an500`. An emulator does not model caches,
flash wait states or the storage, so the CPU figures are a ball-park
guide: at f MHz, n million instructions are roughly n / f seconds.

Running
-------

```sh
ci/run.sh bench                    # measure, and check against the dataset (CI)
ci/run.sh bench cortex-m4          # one platform
ci/run.sh bench update             # measure, and write a new dataset and pages
```

`ci/run.sh bench` fails if a figure is more than 5 % (and 2 units) above
the dataset in [docs/performance/data](../../docs/performance/data), or if
the pages or the README summary are not the ones the dataset makes. Less
is fine. More needs a deliberate new dataset: run `ci/run.sh bench update`
and commit what it writes, so the change in cost is visible in the pull
request.

Files
-----

| File | What |
|---|---|
| [bench.c](bench.c), [bench.h](bench.h) | the workloads, and the hooks a platform provides |
| [platform_mps2.c](platform_mps2.c), [platform_host.c](platform_host.c) | the Cortex-M (QEMU MPS2) and PC platforms |
| [bench_heap.c](bench_heap.c) | the allocation wrappers |
| [table.py](table.py) | makes the pages and the README summary from the dataset, and compares a run with it |
| [../../ci/jobs/bench.sh](../../ci/jobs/bench.sh) | builds, runs, checks or updates |
