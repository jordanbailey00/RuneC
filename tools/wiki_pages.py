#!/usr/bin/env python3
"""OSRS Wiki page-level scraper.

Bucket covers structured rows. The unstructured stuff — boss mechanics,
dialogue trees, quest walkthroughs, clue solutions, per-item special
effects — lives in wikitext on individual pages. Fetch it via
`action=parse&prop=wikitext` and feed it through `mwparserfromhell`
to extract templates.

Caching: each page goes to `tools/wiki_cache/pages/{sanitized}.json`
— key = wiki title (page names are unique). Re-fetches read from cache.

Usage:
    from wiki_pages import PageClient
    c = PageClient()
    wt = c.wikitext("TzTok-Jad")               # raw wikitext string
    tmpls = c.templates("TzTok-Jad")           # mwparserfromhell templates
    info = c.infobox("TzTok-Jad", "Infobox Monster")  # {param: value}

CLI:
    python wiki_pages.py wikitext "TzTok-Jad"
    python wiki_pages.py infobox  "TzTok-Jad" --template "Infobox Monster"
    python wiki_pages.py templates "TzTok-Jad"
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterator

import mwparserfromhell

from wiki_client import WikiClient

PAGES_DIR_NAME = "pages"
_SANITIZE_RE = re.compile(r"[^A-Za-z0-9._-]+")


def _sanitize(title: str) -> str:
    # Keep it short + filesystem-safe; collisions almost impossible for
    # actual OSRS page names (ASCII-dominant). Length cap = 180.
    s = _SANITIZE_RE.sub("_", title).strip("_")
    return s[:180] or "_"


class PageClient(WikiClient):
    """action=parse wikitext fetcher with disk cache + mwparserfromhell."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.pages_dir = self.cache / PAGES_DIR_NAME
        self.pages_dir.mkdir(parents=True, exist_ok=True)

    # ---- Fetchers ----

    def wikitext(self, title: str, refetch: bool = False) -> str:
        """Return the raw wikitext of `title`, reading cache if present."""
        cache_path = self.pages_dir / f"{_sanitize(title)}.json"
        if cache_path.exists() and not refetch:
            d = json.loads(cache_path.read_text())
            return d.get("wikitext", "")
        data = self._get({"action": "parse", "page": title,
                          "prop": "wikitext"})
        # action=parse returns {"parse": {"title", "pageid", "wikitext"}}
        parse = data.get("parse", {})
        wt = parse.get("wikitext") or ""
        # formatversion=2 returns bare string; fallback for older shape.
        if isinstance(wt, dict):
            wt = wt.get("*") or ""
        cache_path.write_text(json.dumps({"title": title,
                                          "pageid": parse.get("pageid"),
                                          "wikitext": wt}))
        if self.verbose:
            print(f"  parsed {title} ({len(wt)} chars)", file=sys.stderr)
        return wt

    def wikitext_many(self, titles: list[str]) -> Iterator[tuple[str, str]]:
        """Fetch pages serially (no batching — action=parse takes one page
        at a time). Yields (title, wikitext)."""
        for t in titles:
            yield t, self.wikitext(t)

    def category_members(self, category: str,
                         namespace: int | None = 0) -> list[str]:
        """Return every page title in `Category:<category>`. Handles
        MediaWiki continuation automatically. `namespace=0` = main/article
        pages only (drops redirects from subcategories, user pages, etc.).
        Pass `namespace=None` to include all namespaces."""
        out: list[str] = []
        cmcontinue = None
        while True:
            params = {
                "action": "query",
                "list": "categorymembers",
                "cmtitle": f"Category:{category}",
                "cmlimit": 500,
            }
            if namespace is not None:
                params["cmnamespace"] = namespace
            if cmcontinue:
                params["cmcontinue"] = cmcontinue
            data = self._get(params)
            for m in data.get("query", {}).get("categorymembers", []):
                out.append(m["title"])
            cmcontinue = (data.get("continue", {}) or {}).get("cmcontinue")
            if not cmcontinue:
                break
        if self.verbose:
            print(f"  Category:{category} → {len(out)} members",
                  file=sys.stderr)
        return out

    # ---- Parsing helpers ----

    def templates(self, title: str) -> list[mwparserfromhell.wikicode.Wikicode]:
        """All {{Template}} invocations on a page."""
        wt = self.wikitext(title)
        code = mwparserfromhell.parse(wt)
        return list(code.filter_templates())

    def infobox(self, title: str, template_name: str) -> dict[str, str]:
        """Extract named params from the first `{{template_name}}` on
        `title`. Case-insensitive on the template name. Returns
        `{param_name: stripped_value}`. Empty if template missing."""
        target = template_name.strip().lower()
        for tmpl in self.templates(title):
            if str(tmpl.name).strip().lower() == target:
                out: dict[str, str] = {}
                for p in tmpl.params:
                    k = str(p.name).strip()
                    v = str(p.value).strip()
                    out[k] = v
                return out
        return {}

    def all_infoboxes(self, title: str,
                      template_name: str) -> list[dict[str, str]]:
        """Same as `infobox` but returns every matching template (some
        pages have multiple Infobox Monster instances for boss phases)."""
        target = template_name.strip().lower()
        out: list[dict[str, str]] = []
        for tmpl in self.templates(title):
            if str(tmpl.name).strip().lower() == target:
                d: dict[str, str] = {}
                for p in tmpl.params:
                    d[str(p.name).strip()] = str(p.value).strip()
                out.append(d)
        return out


# ---- CLI ----

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("probe", help="log rate-limit headroom")

    w = sub.add_parser("wikitext", help="print raw wikitext for a page")
    w.add_argument("title")

    t = sub.add_parser("templates",
                       help="list template names on a page (one per line)")
    t.add_argument("title")

    i = sub.add_parser("infobox",
                       help="dump one template's params as JSON")
    i.add_argument("title")
    i.add_argument("--template", required=True,
                   help="e.g. 'Infobox Monster'")
    i.add_argument("--all", action="store_true",
                   help="return all matches instead of first")

    c_ = sub.add_parser("category",
                        help="list members of a category, one per line")
    c_.add_argument("category")
    c_.add_argument("--namespace", type=int, default=0,
                    help="MediaWiki namespace (0=main pages, default)")

    args = ap.parse_args()
    c = PageClient()

    if args.cmd == "probe":
        c.probe()
        return

    if args.cmd == "wikitext":
        sys.stdout.write(c.wikitext(args.title))
        return

    if args.cmd == "templates":
        for tmpl in c.templates(args.title):
            print(str(tmpl.name).strip())
        return

    if args.cmd == "infobox":
        if args.all:
            sys.stdout.write(json.dumps(
                c.all_infoboxes(args.title, args.template),
                indent=2, ensure_ascii=False))
        else:
            sys.stdout.write(json.dumps(
                c.infobox(args.title, args.template),
                indent=2, ensure_ascii=False))
        sys.stdout.write("\n")
        return

    if args.cmd == "category":
        for title in c.category_members(args.category,
                                        namespace=args.namespace):
            print(title)


if __name__ == "__main__":
    main()
