# SPDX-License-Identifier: BSD-3-Clause
# env: docs
# Build the project site (fork issue #166) into build-ci/pages/site: the
# README, the documents of the repository, the pages generated from the code
# (configuration, tests, fuzzing, CI) and the API reference.
#
#   ci/run.sh pages
#
# Open build-ci/pages/site/index.html, or serve the directory
# (python3 -m http.server -d build-ci/pages/site).
set -eu

out=build-ci/pages
rm -rf "$out"
python3 docs/site/build.py "$out"
mkdir -p "$out/docs/api"
(cat docs/site/Doxyfile; echo "OUTPUT_DIRECTORY = $out/docs/api") |
	doxygen - 2>"$out/doxygen.log" || { cat "$out/doxygen.log"; exit 1; }
mkdocs build --strict -f "$out/mkdocs.yml"
echo "pages: $(find "$out/site" -name '*.html' | wc -l) pages in $out/site"
