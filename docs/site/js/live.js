/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Live data of the project site (fork issue #166): what changes between
 * merges and is not in the repository, fetched by the reader's browser
 * from the GitHub API (no token: 60 requests an hour per address, so the
 * answers are kept for 10 minutes in sessionStorage). Everything in the
 * repository is put on the pages when the site is built instead.
 *
 * An element <div data-live="downloads"></div> becomes the list of the
 * latest release's files, grouped by what they are for. Everything from
 * the API is inserted as text, never as HTML.
 */
(function () {
	"use strict";

	var REPO = "antimatter-studios/lwext4";
	var API = "https://api.github.com/repos/" + REPO;
	var KEEP_MS = 10 * 60 * 1000;

	function get(path) {
		var key = "lwext4-live:" + path;
		try {
			var kept = JSON.parse(sessionStorage.getItem(key));
			if (kept && Date.now() - kept.t < KEEP_MS)
				return Promise.resolve(kept.v);
		} catch (e) { /* no storage: fetch */ }
		return fetch(API + path, {
			headers: { Accept: "application/vnd.github+json" }
		}).then(function (r) {
			if (!r.ok)
				throw new Error("GitHub API: " + r.status);
			return r.json();
		}).then(function (v) {
			try {
				sessionStorage.setItem(key, JSON.stringify(
					{ t: Date.now(), v: v }));
			} catch (e) { /* full or disabled */ }
			return v;
		});
	}

	function el(tag, text, attrs) {
		var e = document.createElement(tag);
		if (text !== undefined && text !== null)
			e.textContent = text;
		for (var k in attrs || {})
			e.setAttribute(k, attrs[k]);
		return e;
	}

	function size(n) {
		return n >= 1048576 ? (n / 1048576).toFixed(1) + " MiB" :
			Math.max(1, Math.round(n / 1024)) + " KiB";
	}

	/* What a release file is for, from its name (see release.yml) */
	var GROUPS = [
		[/-baremetal-sdcard-(.+)\.tar\.gz$/, "Bare-metal firmware, SD card on SPI",
		 "examples/baremetal-sdcard/"],
		[/-esp-idf-(.+)\.tar\.gz$/, "ESP-IDF firmware (ESP32, ESP32-C3, ESP32-S3)",
		 "examples/esp-idf/"],
		[/-zephyr-(.+)\.tar\.gz$/, "Zephyr firmware", "examples/zephyr/"],
		[/-(cortex-m)\.tar\.gz$/, "Library for Cortex-M (M0 to M7)", null],
		[/-(linux-.+|windows-.+)\.tar\.gz$/,
		 "Library and tools for Linux and Windows", null],
		[/^ctest-(.+)\.xml$/, "Test results of the release, per platform", null],
		[/^(MANIFEST\.md|TEST-REPORT\.md)$/,
		 "Changes since the previous release, and the test report", null]
	];

	function downloads(box) {
		var base = box.getAttribute("data-base") || "";
		box.textContent = "Loading the latest release from GitHub…";
		get("/releases/latest").then(function (rel) {
			box.textContent = "";
			var head = el("p");
			head.appendChild(el("strong", rel.tag_name));
			head.appendChild(document.createTextNode(", published " +
				rel.published_at.slice(0, 10) + ". "));
			head.appendChild(el("a", "Release notes",
				{ href: rel.html_url }));
			box.appendChild(head);

			var groups = GROUPS.map(function () { return []; });
			var other = [];
			rel.assets.forEach(function (a) {
				for (var i = 0; i < GROUPS.length; i++) {
					var m = GROUPS[i][0].exec(a.name);
					if (m) {
						groups[i].push({ a: a, what: m[1] });
						return;
					}
				}
				other.push({ a: a, what: a.name });
			});
			GROUPS.concat([[null, "Other files", null]])
				.forEach(function (g, i) {
				var list = i < GROUPS.length ? groups[i] : other;
				if (!list.length)
					return;
				var h = el("h3", g[1]);
				box.appendChild(h);
				if (g[2]) {
					var p = el("p", "Sources and instructions: ");
					p.appendChild(el("a", g[2], { href: base + g[2] }));
					box.appendChild(p);
				}
				var t = el("table");
				var tr = el("tr");
				["For", "File", "Size"].forEach(function (c) {
					tr.appendChild(el("th", c));
				});
				t.appendChild(el("thead")).appendChild(tr);
				var tb = t.appendChild(el("tbody"));
				list.forEach(function (x) {
					var row = el("tr");
					row.appendChild(el("td", x.what));
					var td = el("td");
					td.appendChild(el("a", x.a.name,
						{ href: x.a.browser_download_url }));
					row.appendChild(td);
					row.appendChild(el("td", size(x.a.size)));
					tb.appendChild(row);
				});
				box.appendChild(t);
			});
		}).catch(function (e) {
			box.textContent = "";
			var p = el("p", "Could not ask GitHub (" + e.message +
				"); the files are on ");
			p.appendChild(el("a", "the releases page",
				{ href: "https://github.com/" + REPO + "/releases" }));
			box.appendChild(p);
		});
	}

	function run() {
		document.querySelectorAll('[data-live="downloads"]')
			.forEach(downloads);
	}
	/* Material's instant navigation replaces pages without a reload */
	if (window.document$ && window.document$.subscribe)
		window.document$.subscribe(run);
	else if (document.readyState === "loading")
		document.addEventListener("DOMContentLoaded", run);
	else
		run();
})();
