#!/usr/bin/env python3
"""
map_audit_vision.py — Optional Gemini Vision Visual-QA Pass for Project Hearth Maps

Inspects rendered 16-bit pixel-art map contact sheets (or individual changed zone/
continent PNGs) using the Gemini API (`gemini-4-hydrogen` by default) to catch
aesthetic, composition, and semantic placement flaws that pixel-math rules alone
cannot see (e.g., overlapping sprites, awkward prop placement, unnatural terrain
transitions, jarring tile edges, or unreadable architecture).

Requires GEMINI_API_KEY in the environment (zero external pip dependencies).
"""

import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request

DEFAULT_MODEL = "gemini-4-hydrogen"
API_BASE = "https://generativelanguage.googleapis.com/v1beta/models"

VISION_PROMPT = """You are a senior 16-bit pixel-art QA director reviewing rendered tilemaps from an SNES/Genesis-era 2D JRPG (Project Hearth).

Each attached PNG is either:
- A labeled contact sheet containing multiple numbered 2D town/dungeon/continent maps in a grid, OR
- A single full-resolution zone/continent pixel-art map.

Carefully inspect EVERY map shown for genuine visual defects that hurt 16-bit pixel-art quality or readability:
1. Tile seam mismatches: abrupt rectangular tile edges, unblended coral/water/sand/grass/snow/road boundaries, mismatched biome grass shades.
2. Misplaced props or overlapping art: coral overlapping dry beach/sand, trees sitting directly on stone cobble paths or stairs, palm trees planted on a biome seam, props colliding with doorways/portals/bridges.
3. Broken autotiling or architecture: floating roof eaves, protruding interior wall caps, disconnected fence/bridge rails, cut-off cliffs or ledges, broken shorelines.
4. Repetitive or mechanical grid artifacts: rigid checkerboard/straight-line coral reefs, obvious copy-paste tiling errors.
5. Gameplay readability defects: blocked doorways, obscured staircases, unreadable paths.

Be rigorous and specific, but do NOT invent issues where a map looks clean and intentional.
For each genuine finding, name the exact map label shown in the image header (or filename), give a concrete location hint (e.g. "southwest beach near pier", "north wall right of stairs"), classify severity (CRITICAL, ERROR, WARNING, or NOTE), and provide a concise actionable fix.
If all maps look clean, return overall_status="PASS" with an empty findings array."""

RESPONSE_SCHEMA = {
    "type": "OBJECT",
    "properties": {
        "overall_status": {
            "type": "STRING",
            "enum": ["PASS", "ISSUES_FOUND"],
        },
        "summary": {"type": "STRING"},
        "maps_inspected": {
            "type": "ARRAY",
            "items": {"type": "STRING"},
        },
        "findings": {
            "type": "ARRAY",
            "items": {
                "type": "OBJECT",
                "properties": {
                    "map_name": {"type": "STRING"},
                    "severity": {
                        "type": "STRING",
                        "enum": ["CRITICAL", "ERROR", "WARNING", "NOTE"],
                    },
                    "category": {"type": "STRING"},
                    "location_hint": {"type": "STRING"},
                    "description": {"type": "STRING"},
                    "suggested_fix": {"type": "STRING"},
                },
                "required": [
                    "map_name",
                    "severity",
                    "category",
                    "location_hint",
                    "description",
                    "suggested_fix",
                ],
            },
        },
    },
    "required": ["overall_status", "summary", "maps_inspected", "findings"],
}


def call_gemini_vision(api_key, model, image_paths, max_retries=3):
    parts = [{"text": VISION_PROMPT}]
    for path in image_paths:
        label = os.path.basename(path)
        with open(path, "rb") as fp:
            b64 = base64.b64encode(fp.read()).decode("ascii")
        parts.append({"text": f"Image file: {label}"})
        parts.append({
            "inlineData": {
                "mimeType": "image/png",
                "data": b64,
            }
        })

    payload = {
        "contents": [{"role": "user", "parts": parts}],
        "generationConfig": {
            "responseMimeType": "application/json",
            "responseSchema": RESPONSE_SCHEMA,
        },
    }

    url = f"{API_BASE}/{model}:generateContent"
    body = json.dumps(payload).encode("utf-8")
    headers = {
        "Content-Type": "application/json",
        "x-goog-api-key": api_key,
        "User-Agent": "aistudio-build",
    }

    last_err = None
    for attempt in range(1, max_retries + 1):
        req = urllib.request.Request(url, data=body, headers=headers, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                raw = resp.read().decode("utf-8")
                data = json.loads(raw)
                candidates = data.get("candidates") or []
                if not candidates:
                    raise RuntimeError(f"Gemini returned no candidates: {raw[:400]}")
                text_parts = [
                    p.get("text", "")
                    for p in candidates[0].get("content", {}).get("parts", [])
                    if "text" in p
                ]
                joined = "".join(text_parts).strip()
                if not joined:
                    raise RuntimeError(f"Gemini returned empty text payload: {raw[:400]}")
                return json.loads(joined)
        except urllib.error.HTTPError as e:
            err_body = ""
            try:
                err_body = e.read().decode("utf-8", errors="replace")
            except Exception:
                pass
            last_err = RuntimeError(f"HTTP {e.code} from Gemini API ({model}): {err_body[:500]}")
            if e.code in (429, 500, 502, 503, 504) and attempt < max_retries:
                time.sleep(2.0 * attempt)
                continue
            raise last_err
        except urllib.error.URLError as e:
            last_err = RuntimeError(f"Network error calling Gemini API: {e.reason}")
            if attempt < max_retries:
                time.sleep(2.0 * attempt)
                continue
            raise last_err

    raise last_err or RuntimeError("Gemini Vision request failed")


def main():
    parser = argparse.ArgumentParser(
        description="Run Gemini Vision visual-QA audit on rendered Project Hearth map PNGs."
    )
    parser.add_argument(
        "images",
        nargs="+",
        help="One or more PNG paths (contact sheets or individual zone/continent maps).",
    )
    parser.add_argument(
        "--out",
        default="build/map_audit/vision_report.json",
        help="Output path for structured JSON vision audit report.",
    )
    parser.add_argument(
        "--model",
        default=os.environ.get("GEMINI_VISION_MODEL", DEFAULT_MODEL),
        help=f"Gemini model ID (default: {DEFAULT_MODEL} or GEMINI_VISION_MODEL).",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=3,
        help="Max PNGs per Gemini API request (default: 3).",
    )
    args = parser.parse_args()

    api_key = os.environ.get("GEMINI_API_KEY", "").strip()
    if not api_key:
        print(
            "ERROR: GEMINI_API_KEY is not set in the environment.\n"
            "Set GEMINI_API_KEY before running `make map-audit-vision` or `make map-audit VISION=1`:\n"
            "  export GEMINI_API_KEY=your_api_key_here",
            file=sys.stderr,
        )
        return 2

    missing = [p for p in args.images if not os.path.isfile(p)]
    if missing:
        print(f"ERROR: Image file(s) not found: {', '.join(missing)}", file=sys.stderr)
        return 2

    batch_size = max(1, args.batch_size)
    all_findings = []
    all_inspected = []
    summaries = []

    print(f"Running Gemini Vision visual QA ({args.model}) on {len(args.images)} image(s)...")
    for start in range(0, len(args.images), batch_size):
        batch = args.images[start:start + batch_size]
        names = ", ".join(os.path.basename(p) for p in batch)
        print(f"  Inspecting batch [{start // batch_size + 1}]: {names}")
        try:
            res = call_gemini_vision(api_key, args.model, batch)
        except Exception as exc:
            print(f"ERROR: Vision audit failed: {exc}", file=sys.stderr)
            return 2

        all_inspected.extend(res.get("maps_inspected", []))
        if res.get("summary"):
            summaries.append(res["summary"])
        for f in res.get("findings", []):
            all_findings.append(f)

    crit_err = sum(1 for f in all_findings if f.get("severity") in ("CRITICAL", "ERROR"))
    warn_cnt = sum(1 for f in all_findings if f.get("severity") == "WARNING")
    note_cnt = sum(1 for f in all_findings if f.get("severity") == "NOTE")
    overall = "ISSUES_FOUND" if crit_err > 0 else "PASS"

    report = {
        "model": args.model,
        "images_sent": args.images,
        "overall_status": overall,
        "summary": " ".join(summaries),
        "counts": {
            "critical_or_error": crit_err,
            "warning": warn_cnt,
            "note": note_cnt,
            "total": len(all_findings),
        },
        "maps_inspected": all_inspected,
        "findings": all_findings,
    }

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.out, "w") as fp:
        json.dump(report, fp, indent=2)

    print("------------------------------------------------------------------------")
    print(f"  GEMINI VISION QA ({args.model}) — {overall}")
    print(f"  Findings: CRITICAL/ERROR={crit_err}  WARNING={warn_cnt}  NOTE={note_cnt}")
    for f in all_findings:
        print(
            f"    [{f['severity']:8s}] {f['map_name']} ({f['location_hint']}) "
            f"{f['category']}: {f['description']}"
        )
        print(f"               Fix: {f['suggested_fix']}")
    print(f"  Vision JSON : {args.out}")
    print("------------------------------------------------------------------------")

    return 1 if crit_err > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
