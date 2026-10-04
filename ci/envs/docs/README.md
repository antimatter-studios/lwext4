# docs

Builds on [base](../base). Adds the tools of the project site:

| Package | Why |
|---|---|
| mkdocs, mkdocs-material | the static site, its navigation and search, from Markdown |
| doxygen | the API reference, from the comments in `include/*.h` |

Used by

- `ci/jobs/pages.sh`: `docs/site/build.py` generates the pages from the
  repository, Doxygen and MkDocs build the site into `build-ci/pages/site`.
  `.github/workflows/pages.yml` builds it for every pull request and
  publishes it to GitHub Pages from `main`.
