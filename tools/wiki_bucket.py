#!/usr/bin/env python3
"""OSRS Wiki Bucket query client.

The OSRS Wiki (weirdgloop.org infra) replaced Cargo + Semantic MediaWiki
with a custom "Bucket" extension. All structured data queries go through
`action=bucket&query=<string>` where the string is a mini DSL:

    bucket('name').select('f1','f2').where('field','value').run()

All bucket + field names are lowercase with underscores for spaces.
See `https://meta.weirdgloop.org/w/Bucket` for spec.

Pagination is handled inside the client via `.limit(n).offset(k)` on the
query chain. HTTP / pacing / backoff comes from `wiki_client.WikiClient`.

Usage:
    from wiki_bucket import BucketClient
    c = BucketClient()
    c.probe()                                       # log rate limits
    rows = list(c.fetch("infobox_item",
                        fields=["item_id", "item_name", "examine"]))

CLI:
    python wiki_bucket.py probe
    python wiki_bucket.py fetch infobox_item \\
        --fields item_id,item_name,examine --max-rows 20
"""
from __future__ import annotations

import argparse
import json
import sys
from typing import Any, Iterator

from wiki_client import WikiClient, RateLimited  # noqa: F401 (re-exported)

PAGE_LIMIT = 500


class BucketClient(WikiClient):
    """Bucket DSL queries on top of the shared WikiClient."""

    def raw_query(self, query: str) -> list[dict]:
        """Run a literal Bucket DSL query; return the `bucket` rows."""
        data = self._get({"action": "bucket", "query": query})
        return data.get("bucket", [])

    def fetch(self, bucket: str, fields: list[str],
              where: list[tuple[str, Any]] | None = None,
              limit: int = PAGE_LIMIT) -> Iterator[dict]:
        """Yield every row of a Bucket table matching the query.

        Paginates via `.limit(n).offset(k)`; stops when a page returns
        < limit rows. Each page cached under
        wiki_cache/{bucket}_{hash}_{offset}.json.
        """
        where = where or []
        qhash = self._hash({"bucket": bucket, "fields": fields,
                            "where": where, "limit": limit})

        offset = 0
        pages = 0
        total = 0
        while True:
            cache_path = self.cache / f"{bucket}_{qhash}_{offset:06d}.json"
            if cache_path.exists():
                page = json.loads(cache_path.read_text())
                rows = page.get("bucket", [])
            else:
                q = self._build_query(bucket, fields, where, limit, offset)
                page = self._get({"action": "bucket", "query": q})
                rows = page.get("bucket", [])
                cache_path.write_text(json.dumps(page))
                if self.verbose:
                    print(f"  {bucket} offset={offset} rows={len(rows)} "
                          f"(fetched)", file=sys.stderr)

            for r in rows:
                yield r
                total += 1

            pages += 1
            if len(rows) < limit:
                break
            offset += limit

        if self.verbose:
            print(f"  {bucket}: {pages} pages, {total} rows total",
                  file=sys.stderr)

    @staticmethod
    def _build_query(bucket: str, fields: list[str],
                     where: list[tuple[str, Any]],
                     limit: int, offset: int) -> str:
        parts = [f"bucket('{bucket}')"]
        sel = ",".join(f"'{f}'" for f in fields)
        parts.append(f".select({sel})")
        for (field, val) in where:
            if isinstance(val, str):
                vrepr = f"'{val}'"
            else:
                vrepr = str(val)
            parts.append(f".where('{field}',{vrepr})")
        parts.append(f".limit({limit})")
        if offset:
            parts.append(f".offset({offset})")
        parts.append(".run()")
        return "".join(parts)


# ---- CLI ----

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("probe", help="log rate-limit headroom")

    f = sub.add_parser("fetch", help="run a bucket query, dump JSONL to stdout")
    f.add_argument("bucket")
    f.add_argument("--fields", required=True,
                   help="comma-separated field list")
    f.add_argument("--where", default=None,
                   help="k=v pairs separated by '&' (e.g. 'members=true&id=4151')")
    f.add_argument("--limit", type=int, default=PAGE_LIMIT)
    f.add_argument("--max-rows", type=int, default=0,
                   help="stop after N rows total (0 = no cap)")

    r = sub.add_parser("raw", help="run a literal Bucket DSL query")
    r.add_argument("query")

    args = ap.parse_args()
    c = BucketClient()

    if args.cmd == "probe":
        c.probe()
        return

    if args.cmd == "raw":
        rows = c.raw_query(args.query)
        for row in rows:
            sys.stdout.write(json.dumps(row) + "\n")
        print(f"# {len(rows)} rows", file=sys.stderr)
        return

    if args.cmd == "fetch":
        fields = [s.strip() for s in args.fields.split(",") if s.strip()]
        where: list[tuple[str, Any]] = []
        if args.where:
            for pair in args.where.split("&"):
                k, _, v = pair.partition("=")
                where.append((k, _parse_scalar(v)))
        n = 0
        for row in c.fetch(args.bucket, fields, where=where,
                           limit=args.limit):
            sys.stdout.write(json.dumps(row) + "\n")
            n += 1
            if args.max_rows and n >= args.max_rows:
                break
        print(f"# {n} rows", file=sys.stderr)


def _parse_scalar(s: str) -> Any:
    if s.lower() == "true":
        return True
    if s.lower() == "false":
        return False
    try:
        return int(s)
    except ValueError:
        return s


if __name__ == "__main__":
    main()
