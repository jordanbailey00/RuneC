#!/usr/bin/env python3
"""Shared HTTP client for OSRS Wiki MediaWiki API.

Handles pacing, maxlag, exponential backoff, User-Agent, and the
`userinfo` rate-limit probe. Both `wiki_bucket.py` (Bucket DSL) and
`wiki_pages.py` (action=parse) subclass this so they share one
pacing clock when used in the same process.

Per MediaWiki `API:Etiquette`:
 - serial only (no parallel requests)
 - `maxlag=5` on every request
 - custom User-Agent with contact info
 - exponential backoff on ratelimited / 429 / 503
"""
from __future__ import annotations

import hashlib
import json
import sys
import time
from pathlib import Path

import requests

API = "https://oldschool.runescape.wiki/api.php"
UA = ("RuneC-data-builder/0.1 (jordanbaileypmp@gmail.com) "
      "python-requests/2.31")
CACHE = Path(__file__).resolve().parent / "wiki_cache"

MAXLAG = 5
BACKOFF_BASE = 1.0
BACKOFF_CAP = 60.0
MIN_INTERVAL = 0.5


class RateLimited(Exception):
    pass


class PageMissing(Exception):
    """Raised when the wiki returns `missingtitle` for action=parse.
    Scrapers catch this to skip pages that don't exist (e.g. bosses
    without a /Strategies subpage)."""
    def __init__(self, title: str):
        super().__init__(f"page does not exist: {title}")
        self.title = title


class WikiClient:
    """Rate-limited MediaWiki API client. Subclass and add action-
    specific methods on top."""

    def __init__(self, cache_dir: Path = CACHE, verbose: bool = True):
        self.cache = cache_dir
        self.cache.mkdir(parents=True, exist_ok=True)
        self.session = requests.Session()
        self.session.headers["User-Agent"] = UA
        self.verbose = verbose
        self._last_request_at = 0.0

    # ---- HTTP ----

    def _pace(self):
        dt = time.monotonic() - self._last_request_at
        if dt < MIN_INTERVAL:
            time.sleep(MIN_INTERVAL - dt)

    def _get(self, params: dict) -> dict:
        params = {**params, "format": "json", "formatversion": "2",
                  "maxlag": MAXLAG}
        attempt = 0
        while True:
            self._pace()
            r = self.session.get(API, params=params, timeout=30)
            self._last_request_at = time.monotonic()

            if r.status_code in (429, 503):
                wait = self._backoff(attempt, r)
                if self.verbose:
                    print(f"  http {r.status_code} — sleeping {wait:.1f}s",
                          file=sys.stderr)
                time.sleep(wait)
                attempt += 1
                if attempt > 8:
                    raise RateLimited(f"gave up after {attempt} retries")
                continue

            r.raise_for_status()
            data = r.json()

            if "error" in data:
                err = data["error"]
                if isinstance(err, dict):
                    code = err.get("code", "")
                    msg = err.get("info", str(err))
                else:
                    code = ""
                    msg = str(err)
                if code in ("maxlag", "ratelimited"):
                    wait = self._backoff(attempt, r)
                    if self.verbose:
                        print(f"  wiki error {code} — sleeping {wait:.1f}s",
                              file=sys.stderr)
                    time.sleep(wait)
                    attempt += 1
                    if attempt > 8:
                        raise RateLimited(f"gave up after {attempt} retries")
                    continue
                if code == "missingtitle":
                    title = params.get("page") or params.get("title") or ""
                    raise PageMissing(str(title))
                raise RuntimeError(f"wiki error: {msg}")

            return data

    def _backoff(self, attempt: int, r: requests.Response) -> float:
        ra = r.headers.get("Retry-After")
        if ra:
            try:
                return float(ra)
            except ValueError:
                pass
        return min(BACKOFF_CAP, BACKOFF_BASE * (2 ** attempt))

    # ---- Shared public ----

    def probe(self) -> dict:
        """Log rate-limit headroom. Run once at pipeline start."""
        data = self._get({"action": "query", "meta": "userinfo",
                          "uiprop": "ratelimits"})
        info = data.get("query", {}).get("userinfo", {})
        rl = info.get("ratelimits", {})
        if self.verbose:
            print(f"anon user id={info.get('id')} name={info.get('name')}",
                  file=sys.stderr)
            if rl:
                for k, v in rl.items():
                    print(f"  ratelimit {k}: {v}", file=sys.stderr)
            else:
                print("  (no explicit ratelimit block — anon defaults apply)",
                      file=sys.stderr)
        return info

    @staticmethod
    def _hash(d: dict) -> str:
        s = json.dumps(d, sort_keys=True, default=str)
        return hashlib.sha1(s.encode()).hexdigest()[:10]
