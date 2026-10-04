#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Generate the project site's sources from the repository (fork issue #166).

  docs/site/build.py OUT

writes OUT/mkdocs.yml and OUT/docs/: the README as the home page, the
repository's other documents (CONTRIBUTING.md, the READMEs of ci/, the
examples, the ports, the fuzzers, docs/performance), and pages generated
from the code:

  configuration.md    the build options of include/ext4_config.h
  testing/index.md    every regression test, from its leading comment
  testing/fuzzing.md  the fuzzers' README and every crash input kept
  ci/index.md         ci/README.md, the jobs and the workflows
  downloads.md        the latest release's files, filled in by the
                      reader's browser from the GitHub API (js/live.js)

Links between documents that are on the site point to their pages; links to
anything else in the repository point to it on GitHub, at the commit the
site is built from. A link to nothing fails the build (mkdocs --strict).
Nothing here is edited by hand: change the source and rebuild
(ci/run.sh pages).
"""
import glob
import os
import re
import subprocess
import sys

REPO = "antimatter-studios/lwext4"
UPSTREAM = "gkostka/lwext4"
GITHUB = "https://github.com/"
SITE = "https://antimatter-studios.github.io/lwext4/"


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def commit():
    sha = os.environ.get("GITHUB_SHA")
    if sha:
        return sha
    try:
        return subprocess.check_output(
            ["git", "-c", "safe.directory=*", "rev-parse", "HEAD"],
            stderr=subprocess.DEVNULL, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "main"


REF = commit()


def source_url(path):
    kind = "tree" if os.path.isdir(path) else "blob"
    return "%s%s/%s/%s/%s" % (GITHUB, REPO, kind, REF, path)


def issue_links(text):
    """Issue and pull request numbers as links: "fork issue #N" in this
    repository, a plain "issue #N" (the upstream ones the first regression
    tests were written for) in gkostka/lwext4."""
    def sub(m):
        fork, word, num = m.group(1), m.group(2), m.group(3)
        repo = REPO if fork else UPSTREAM
        kind = "issues" if word == "issue" else "pull"
        return "[%s%s #%s](%s%s/%s/%s)" % (fork or "", word, num, GITHUB,
                                           repo, kind, num)
    return re.sub(r"\b(fork )?(issue|PR) #(\d+)", sub, text)


class Site:
    def __init__(self, out):
        self.out = out
        self.docs = os.path.join(out, "docs")
        self.pages = {}  # repository document -> page

    def map(self, src, page):
        if os.path.exists(src):
            self.pages[os.path.normpath(src)] = page

    def link(self, target, src, page):
        """TARGET of a link in SRC, for the page PAGE."""
        if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I) or \
                target.startswith("#"):
            return target
        path, _, anchor = target.partition("#")
        path = os.path.normpath(os.path.join(os.path.dirname(src), path))
        if path in self.pages:
            rel = os.path.relpath(self.pages[path],
                                  os.path.dirname(page) or ".")
            return rel + ("#" + anchor if anchor else "")
        if os.path.exists(path):
            return source_url(path) + ("#" + anchor if anchor else "")
        return target  # broken: mkdocs --strict reports it

    def convert(self, text, src, page):
        """SRC's Markdown TEXT with its links made right for PAGE."""
        out, fence = [], None
        for line in text.split("\n"):
            prev = out[-1] if out else ""
            # GitHub starts a list or a code block right after a paragraph
            # line, Python-Markdown only after a blank line
            para = prev.strip() and not re.match(r"^(\s|[-*+] |\d+\. |>)",
                                                 prev)
            m = re.match(r"^\s*(```+|~~~+)", line)
            if fence is None and para and \
                    (m or re.match(r"^([-*+]|\d+\.) ", line)):
                out.append("")
            if m:
                if fence is None:
                    fence = m.group(1)
                elif m.group(1).startswith(fence):
                    fence = None
            elif fence is None:
                line = re.sub(
                    r"(\]\()([^)\s]+)(\))",
                    lambda m: m.group(1) + self.link(m.group(2), src, page) +
                    m.group(3), line)
                line = re.sub(
                    r"^(\s*\[[^\]]+\]:\s*)(\S+)",
                    lambda m: m.group(1) + self.link(m.group(2), src, page),
                    line)
            out.append(line)
        return "\n".join(out)

    def write(self, page, text):
        path = os.path.join(self.docs, page)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text.rstrip("\n") + "\n")

    def copy(self, src, extra=""):
        page = self.pages[os.path.normpath(src)]
        text = self.convert(read(src), src, page)
        if extra:
            text = text.rstrip("\n") + "\n\n" + extra
        text += "\n\n---\n*Source: [%s](%s)*\n" % (src, source_url(src))
        self.write(page, text)


def comment(path, prefix):
    """The leading comment of a C file (PREFIX "/*") or a shell script
    (PREFIX "#"), without SPDX lines, as paragraphs."""
    lines = read(path).split("\n")
    text = []
    if prefix == "#":
        for line in lines[1 if lines and lines[0].startswith("#!") else 0:]:
            if not line.startswith("#"):
                break
            text.append(line[1:].strip())
    else:
        inside = False
        for line in lines:
            s = line.strip()
            if not inside:
                if s.startswith("/*"):
                    inside = True
                    s = s[2:]
                elif s:
                    break
                else:
                    continue
            end = "*/" in s
            s = s.split("*/")[0].lstrip("*").strip()
            text.append(s)
            if end:
                inside = False
    text = [t for t in text if "SPDX-License-Identifier" not in t]
    paras, cur = [], []
    for t in text + [""]:
        if t:
            cur.append(t)
        elif cur:
            paras.append(" ".join(cur))
            cur = []
    return paras


def cell(text):
    return text.replace("|", "\\|").replace("<", "&lt;")


def configuration(site):
    """The build options: every CONFIG_ default in ext4_config.h with the
    @brief before it."""
    src = "include/ext4_config.h"
    text = read(src)
    rows = []
    pat = re.compile(r"/\*\*\s*@brief\s*(.*?)\*/\s*#ifndef (CONFIG_\w+)\s*"
                     r"#define \2[ \t]*([^\n]*)\n", re.S)
    for m in pat.finditer(text):
        desc = " ".join(re.sub(r"\n\s*\*", " ", m.group(1)).split())
        rows.append("| `%s` | `%s` | %s |" % (m.group(2),
                                             m.group(3).strip() or "",
                                             cell(desc)))
    site.write("configuration.md", """# Configuration

lwext4 is configured at build time with `CONFIG_` macros. Each has a
default in [`include/ext4_config.h`](%s); set them with `-D` on the compiler
command line, or in `generated/ext4_config.h`, which the CMake build writes
(with the target's options, see CMakeLists.txt) unless
`CONFIG_USE_DEFAULT_CFG` is set. With CMake, add or override options with
`LWEXT4_CONFIG`:

```sh
cmake -DLWEXT4_CONFIG="CONFIG_DEBUG_PRINTF=0;CONFIG_DEBUG_ASSERT=0" ...
```

The feature level selects the filesystem features the library supports:
`CONFIG_EXT_FEATURE_SET_LVL` is `F_SET_EXT2`, `F_SET_EXT3` or `F_SET_EXT4`
(the default).

| Option | Default | What it does |
|---|---|---|
%s

---
*Generated from [%s](%s) by docs/site/build.py.*
""" % (source_url(src), "\n".join(rows), src, source_url(src)))


def tests(site):
    rows = []
    for path in sorted(glob.glob("tests/test_*.c")):
        name = os.path.basename(path)[:-2]
        paras = comment(path, "/*")
        what = paras[0] if paras else ""
        what = re.sub(r"^Regression test for ", "", what)
        setup = path[:-2] + ".sh"
        extra = " ([image](%s))" % source_url(setup) \
            if os.path.exists(setup) else ""
        rows.append("| [%s](%s)%s | %s |" % (name, source_url(path), extra,
                                            cell(issue_links(what))))
    site.write("testing/index.md", """# Tests

Every change to lwext4 is tested in CI on every platform it supports
(see [CI](../ci/index.md)): the regression tests below, the e2fsprogs round
trip (lwext4 writes, `e2fsck` checks; `mke2fs` writes, lwext4 reads), the
examples, the [fuzzers](fuzzing.md), and the build of every package.

A fix comes with a regression test that fails without it: the `red-green`
check runs the new tests against the pull request's base and requires them
to fail there and pass with the change.

## Regression tests

%d tests in [`tests/`](%s), each a C program run by CTest; most have a
script that makes their disk image with `mke2fs` and `debugfs` first, and
some a `.check.sh` that checks the result with `e2fsck`. Run them with
`ci/run.sh native` or, after a CMake build, `ctest`.

| Test | What it checks |
|---|---|
%s

---
*Generated from the leading comment of each test by docs/site/build.py.*
""" % (len(rows), source_url("tests"), "\n".join(rows)))


def fuzzing(site):
    rows = []
    for path in sorted(glob.glob("tests/fuzz/crashes/*")):
        name = os.path.basename(path)
        m = re.match(r"^(\d+)-(.*?)(\.gz)?$", name)
        if m:
            what = m.group(2).replace("-", " ")
            issue = "[#%s](%s%s/issues/%s)" % (m.group(1), GITHUB, REPO,
                                               m.group(1))
        else:
            what, issue = name, ""
        rows.append("| [%s](%s) | %s | %s |" % (name, source_url(path),
                                               issue, cell(what)))
    extra = """## Crash inputs kept

Every input that crashed, hung or leaked in a fuzzer, with the issue of
its fix. CI's `fuzz-replay` check runs each of them on every pull request.

| Input | Issue | What it found |
|---|---|---|
%s
""" % "\n".join(rows)
    site.copy("tests/fuzz/README.md", extra)


def ci(site):
    jobs = []
    for path in sorted(glob.glob("ci/jobs/*.sh")):
        lines = read(path).split("\n")
        env = next((l.split(":", 1)[1].strip() for l in lines
                    if l.startswith("# env:")), "")
        desc = comment(path, "#")
        what = re.sub(r"^((env|platform): \S+\s*)+", "", desc[0]) \
            if desc else ""
        envlink = "[%s](envs/%s.md)" % (env, env) \
            if os.path.exists("ci/envs/%s/README.md" % env) else env
        jobs.append("| [%s](%s) | %s | %s |" % (
            os.path.basename(path)[:-3], source_url(path), envlink,
            cell(issue_links(what))))
    flows = []
    for path in sorted(glob.glob(".github/workflows/*.yml")):
        text = read(path)
        name = re.search(r"^name:\s*(.*)$", text, re.M)
        paras = comment(path, "#")
        flows.append("| [%s](%s) | %s |" % (
            name.group(1).strip() if name else os.path.basename(path),
            source_url(path), cell(paras[0] if paras else "")))
    extra = """## Jobs

`ci/run.sh <job>` runs one in its environment.

| Job | Environment | What it does |
|---|---|---|
%s

## Workflows

What GitHub Actions runs, each a call of `ci/run.sh`.

| Workflow | What it runs |
|---|---|
%s
""" % ("\n".join(jobs), "\n".join(flows))
    site.copy("ci/README.md", extra)
    for env in sorted(glob.glob("ci/envs/*/README.md")):
        site.copy(env)


def downloads(site):
    """The page of the latest release's files; docs/site/js/live.js fills
    it in the reader's browser, so it is current without a rebuild."""
    import shutil
    js = os.path.join(site.docs, "js")
    os.makedirs(js, exist_ok=True)
    for path in glob.glob("docs/site/js/*.js"):
        shutil.copy(path, js)
    site.write("downloads.md", """# Downloads

Every release has the library built for each platform CI tests, the
example firmware, and the test results it was released with. This list is
read from GitHub when you open the page, so it always shows the latest
release; all releases are on [GitHub](%s%s/releases).

<div data-live="downloads" data-base="../"></div>

<noscript>The list needs JavaScript: see the
<a href="%s%s/releases">releases on GitHub</a>.</noscript>
""" % (GITHUB, REPO, GITHUB, REPO))


def licence(site):
    site.write("licence.md", """# Licence

The library is BSD-3-Clause, except `src/ext4_extent.c` and
`src/ext4_xattr.c`, which are GPL-2.0 (see their headers). Tests, CI
scripts and the rest of the tooling are BSD-3-Clause.

```text
%s
```

---
*Source: [LICENSE](%s)*
""" % (read("LICENSE").rstrip("\n"), source_url("LICENSE")))


CONFIG = """\
# Generated by docs/site/build.py: do not edit.
site_name: lwext4
site_description: ext2/3/4 filesystem library for microcontrollers
site_url: %(site)s
repo_url: %(github)s%(repo)s
repo_name: %(repo)s
edit_uri: ""
docs_dir: docs
site_dir: site
copyright: >-
  lwext4 is BSD-3-Clause (ext4_extent.c and ext4_xattr.c GPL-2.0).
  This site is generated from commit
  <a href="%(github)s%(repo)s/tree/%(ref)s">%(short)s</a>.
theme:
  name: material
  features:
    - navigation.sections
    - navigation.indexes
    - navigation.top
    - search.highlight
    - content.code.copy
  palette:
    - media: "(prefers-color-scheme: light)"
      scheme: default
      toggle:
        icon: material/weather-night
        name: Dark mode
    - media: "(prefers-color-scheme: dark)"
      scheme: slate
      toggle:
        icon: material/weather-sunny
        name: Light mode
markdown_extensions:
  - tables
  - attr_list
  - admonition
  - toc:
      permalink: true
  - pymdownx.highlight
  - pymdownx.superfences
extra_javascript:
  - js/live.js
validation:
  omitted_files: warn
  absolute_links: warn
  unrecognized_links: warn
nav:
%(nav)s
"""


def nav(items, depth=1):
    out = []
    for title, value in items:
        if isinstance(value, list):
            out.append("%s- %s:" % ("  " * depth, title))
            out.append(nav(value, depth + 1))
        else:
            out.append("%s- %s: %s" % ("  " * depth, title, value))
    return "\n".join(out)


def readme_title(path, default):
    for line in read(path).split("\n"):
        m = re.match(r"^#\s+(.*)$", line)
        if m:
            return m.group(1).strip().strip("`")
        if line.strip() and not line.startswith(("<", "[", "!")):
            return line.strip().strip("`")
    return default


def main(argv):
    if len(argv) != 1:
        sys.stderr.write(__doc__)
        return 2
    site = Site(argv[0])
    os.makedirs(site.docs, exist_ok=True)

    site.map("README.md", "index.md")
    site.map("CONTRIBUTING.md", "contributing.md")
    site.map("LICENSE", "licence.md")
    site.map("include/ext4_config.h", "configuration.md")
    site.map("tests/fuzz/README.md", "testing/fuzzing.md")
    site.map("ci/README.md", "ci/index.md")
    for env in glob.glob("ci/envs/*/README.md"):
        site.map(env, "ci/envs/%s.md" % env.split("/")[2])
    site.map("examples/README.md", "examples/index.md")
    examples = sorted(glob.glob("examples/*/README.md"))
    ports = sorted(glob.glob("ports/*/README.md"))
    for path in examples + ports:
        site.map(path, "%s/%s.md" % tuple(path.split("/")[:2]))
    perf = sorted(glob.glob("docs/performance/*.md"))
    for path in perf:
        site.map(path, "performance/" + (
            "index.md" if path.endswith("/README.md")
            else os.path.basename(path)))
    site.map("tests/bench/README.md", "performance/benchmark.md")

    site.copy("README.md")
    site.copy("CONTRIBUTING.md")
    for path in ["examples/README.md"] + examples + ports + perf:
        site.copy(path)
    if os.path.exists("tests/bench/README.md"):
        site.copy("tests/bench/README.md")
    configuration(site)
    tests(site)
    fuzzing(site)
    ci(site)
    downloads(site)
    licence(site)

    items = [("Home", "index.md"), ("Downloads", "downloads.md"),
             ("Configuration", "configuration.md"),
             ("API reference", "api/index.html")]
    items.append(("Examples", [("Overview", "examples/index.md")] + [
        (readme_title(p, p.split("/")[1]), site.pages[p]) for p in examples]))
    if ports:
        items.append(("Ports", [(readme_title(p, p.split("/")[1]),
                                 site.pages[p]) for p in ports]))
    if perf:
        sub = [("Overview", "performance/index.md")]
        sub += [(os.path.basename(p)[:-3], site.pages[p]) for p in perf
                if not p.endswith("/README.md")]
        if os.path.exists("tests/bench/README.md"):
            sub.append(("The benchmark", "performance/benchmark.md"))
        items.append(("Performance", sub))
    items.append(("Testing", [("Tests", "testing/index.md"),
                              ("Fuzzing", "testing/fuzzing.md")]))
    envs = sorted(glob.glob("ci/envs/*/README.md"))
    items.append(("CI", [("Overview", "ci/index.md"), ("Environments", [
        (p.split("/")[2], site.pages[p]) for p in envs])]))
    items.append(("Contributing", "contributing.md"))
    items.append(("Licence", "licence.md"))

    with open(os.path.join(site.out, "mkdocs.yml"), "w") as f:
        f.write(CONFIG % {"site": SITE, "github": GITHUB, "repo": REPO,
                          "ref": REF, "short": REF[:12],
                          "nav": nav(items)})
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
