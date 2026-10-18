#!/usr/bin/env python3
"""
Builds the unified SNES Game Metadata Database (`config/games_db.json`) for Snex9x v2.

Merges metadata from:
1. OpenVGDB (SNES JSON extract: CRC32, Clean Title, No-Intro Name, Date, Description, Developer, Genre, Region)
2. lelexiao/RetroPie_NoIntro_Rom_Images (ScreenScraper No-Intro XML: Date, Synopsis, Developer, Publisher, Players)
3. dd900/FE-Metadata (LaunchBox SNES JSON: ReleaseYear, Overview, AlternateNames, MaxPlayers)
4. libretro/libretro-database (No-Intro .dat & releaseyear .dat: Canonical CRC32s & Release Years)

Also computes the exact sanitized `Named_Boxarts` filename (`&*/:`<>?\|"` -> `_`) for `libretro-thumbnails`.
"""

import json
import os
import re
import ssl
import sys
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

OPENVGDB_URL = "https://cdn.jsdelivr.net/npm/openvgdb-snes@1.0.2/db.json"
SCREENSCRAPER_XML_URL = "https://raw.githubusercontent.com/lelexiao/RetroPie_NoIntro_Rom_Images/master/NoIntro_Full/gamelists/snes/gamelist.xml"
LAUNCHBOX_JSON_URL = "https://raw.githubusercontent.com/dd900/FE-Metadata/main/%23Launchbox%20Metadata.zip/Super%20Nintendo%20Entertainment%20System.json"
LIBRETRO_NOINTRO_URL = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System.dat"
LIBRETRO_YEAR_URL = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/releaseyear/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System.dat"

GENERIC_DESC_RE = re.compile(r"is a .+ game,\s*published by .+ which was released in", re.IGNORECASE)
YEAR_RE = re.compile(r"\b(19[89]\d|20[0-2]\d)\b")
SANITIZE_BOXART_RE = re.compile(r'[&*/:`<>?\\|"]')


def fetch_text(url: str) -> str:
    print(f"[fetch] {url} ...")
    ctx = ssl.create_default_context()
    req = urllib.request.Request(url, headers={"User-Agent": "Snex9x-v2-DB-Builder/1.0"})
    with urllib.request.urlopen(req, context=ctx, timeout=60) as resp:
        return resp.read().decode("utf-8", errors="replace")


def sanitize_boxart_filename(no_intro_name: str) -> str:
    if not no_intro_name:
        return ""
    return SANITIZE_BOXART_RE.sub("_", no_intro_name) + ".png"


def normalize_title_key(title: str) -> str:
    if not title:
        return ""
    t = title.strip()
    # Convert "Legend of Zelda, The - A Link..." -> "the legend of zelda a link..."
    t = re.sub(r"^(.+?),\s*The(\b.*)$", r"The \1\2", t, flags=re.IGNORECASE)
    # Strip parenthetical/bracket tags
    t = re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", t)
    # Keep only alphanumeric lowercase
    return re.sub(r"[^a-z0-9]+", "", t.lower())


def clean_display_title(no_intro_name: str) -> str:
    if not no_intro_name:
        return "Unknown Game"
    t = re.sub(r"\s*\([^)]*\)", "", no_intro_name).strip()
    t = re.sub(r"\s*\[[^\]]*\]", "", t).strip()
    t = re.sub(r"^(.+?),\s*The(\s+-.*|$)", r"The \1\2", t)
    return t or no_intro_name


def infer_region(no_intro_name: str, fallback: str = "") -> str:
    if fallback and fallback not in ("N/A", "Unknown"):
        return fallback
    m = re.search(r"\((USA|Japan|Europe|World|Germany|France|Spain|Italy|Australia|Korea|Brazil|Canada|Asia)[^)]*\)", no_intro_name, re.IGNORECASE)
    if m:
        return m.group(1)
    return fallback or "Unknown"


def extract_year(raw_val) -> str:
    if not raw_val:
        return ""
    s = str(raw_val).strip()
    if len(s) >= 4 and s[:4].isdigit() and (s.startswith("19") or s.startswith("20")):
        return s[:4]
    m = YEAR_RE.search(s)
    return m.group(1) if m else ""


def parse_clrmamepro_dat(text: str):
    """Parses libretro .dat blocks into a list of dicts with name, crc, releaseyear, region."""
    entries = []
    block_re = re.compile(r"game\s*\((.*?)\n\)", re.DOTALL)
    for m in block_re.finditer(text):
        body = m.group(1)
        entry = {}
        name_m = re.search(r'^\s*name\s+"([^"]+)"', body, re.MULTILINE)
        comment_m = re.search(r'^\s*comment\s+"([^"]+)"', body, re.MULTILINE)
        region_m = re.search(r'^\s*region\s+"([^"]+)"', body, re.MULTILINE)
        year_m = re.search(r'^\s*releaseyear\s+"?(\d{4})"?', body, re.MULTILINE)
        crc_m = re.search(r'\bcrc\s+([0-9A-Fa-f]{8})\b', body)

        if name_m:
            entry["name"] = name_m.group(1)
        elif comment_m:
            entry["name"] = comment_m.group(1)
        if region_m:
            entry["region"] = region_m.group(1)
        if year_m:
            entry["year"] = year_m.group(1)
        if crc_m:
            entry["crc32"] = crc_m.group(1).upper()
        if entry.get("name") or entry.get("crc32"):
            entries.append(entry)
    return entries


def main():
    root_dir = Path(__file__).resolve().parent.parent
    config_dir = root_dir / "config"
    config_dir.mkdir(parents=True, exist_ok=True)
    out_path = config_dir / "games_db.json"

    # 1. Load OpenVGDB SNES JSON
    openvgdb_rows = json.loads(fetch_text(OPENVGDB_URL))
    print(f"[OpenVGDB] Loaded {len(openvgdb_rows)} entries")

    # 2. Load ScreenScraper No-Intro XML (lelexiao)
    ss_by_nointro = {}
    try:
        xml_text = fetch_text(SCREENSCRAPER_XML_URL)
        xml_root = ET.fromstring(xml_text)
        for game_el in xml_root.findall("game"):
            path_txt = (game_el.findtext("path") or "").strip()
            stem = Path(path_txt).stem if path_txt else ""
            if not stem:
                continue
            desc = (game_el.findtext("desc") or "").strip()
            rel_date = (game_el.findtext("releasedate") or "").strip()
            year = extract_year(rel_date)
            dev = (game_el.findtext("developer") or "").strip()
            pub = (game_el.findtext("publisher") or "").strip()
            genre = (game_el.findtext("genre") or "").strip()
            players = (game_el.findtext("players") or "").strip()
            ss_by_nointro[stem] = {
                "year": year,
                "description": desc,
                "developer": dev,
                "publisher": pub,
                "genre": genre,
                "players": players,
            }
        print(f"[ScreenScraper XML] Loaded {len(ss_by_nointro)} No-Intro entries")
    except Exception as e:
        print(f"[warn] ScreenScraper XML fetch failed: {e}")

    # 3. Load LaunchBox SNES JSON (dd900/FE-Metadata)
    lb_by_norm_title = {}
    try:
        lb_raw = json.loads(fetch_text(LAUNCHBOX_JSON_URL))
        lb_games = lb_raw if isinstance(lb_raw, list) else lb_raw.get("LaunchBox", {}).get("Game", [])
        if isinstance(lb_raw, dict) and not lb_games:
            # Handle top-level key dictionary or array
            for v in lb_raw.values():
                if isinstance(v, list):
                    lb_games = v
                    break
        for g in lb_games:
            if not isinstance(g, dict):
                continue
            name = g.get("Name") or g.get("name") or ""
            overview = (g.get("Overview") or g.get("overview") or "").strip()
            year = extract_year(g.get("ReleaseYear") or g.get("ReleaseDate") or "")
            dev = (g.get("Developer") or "").strip()
            pub = (g.get("Publisher") or "").strip()
            genre = (g.get("Genres") or "").strip()
            players = str(g.get("MaxPlayers") or "").strip()
            rec = {
                "year": year,
                "description": overview,
                "developer": dev,
                "publisher": pub,
                "genre": genre,
                "players": players,
            }
            for candidate in [name] + (g.get("AlternateNames") if isinstance(g.get("AlternateNames"), list) else []):
                if isinstance(candidate, dict):
                    candidate = candidate.get("Name", "")
                key = normalize_title_key(str(candidate))
                if key and (key not in lb_by_norm_title or (overview and not lb_by_norm_title[key]["description"])):
                    lb_by_norm_title[key] = rec
        print(f"[LaunchBox JSON] Indexed {len(lb_by_norm_title)} normalized title keys")
    except Exception as e:
        print(f"[warn] LaunchBox JSON fetch failed: {e}")

    # 4. Load Libretro No-Intro & ReleaseYear .dat
    lr_nointro = {}
    lr_years_by_crc = {}
    lr_years_by_name = {}
    try:
        for e in parse_clrmamepro_dat(fetch_text(LIBRETRO_NOINTRO_URL)):
            if e.get("crc32"):
                lr_nointro[e["crc32"]] = e
        for e in parse_clrmamepro_dat(fetch_text(LIBRETRO_YEAR_URL)):
            if e.get("year"):
                if e.get("crc32"):
                    lr_years_by_crc[e["crc32"]] = e["year"]
                if e.get("name"):
                    lr_years_by_name[e["name"]] = e["year"]
        print(f"[Libretro DAT] Loaded {len(lr_nointro)} No-Intro CRC32s and {len(lr_years_by_crc)} release years")
    except Exception as e:
        print(f"[warn] Libretro DAT fetch failed: {e}")

    # 5. Merge into unified records keyed by uppercase CRC32
    games_by_crc = {}

    for row in openvgdb_rows:
        crc = (row.get("romHashCRC") or "").strip().upper()
        if not crc:
            continue
        no_intro = (row.get("romExtensionlessFileName") or "").strip()
        if not no_intro and row.get("romFileName"):
            no_intro = Path(row["romFileName"]).stem
        title = (row.get("releaseTitleName") or "").strip() or clean_display_title(no_intro)
        ovg_desc = (row.get("releaseDescription") or "").strip()
        ovg_year = extract_year(row.get("releaseDate") or "")
        region = infer_region(no_intro, (row.get("regionName") or row.get("TEMPromRegion") or "").strip())
        dev = (row.get("releaseDeveloper") or "").strip()
        pub = (row.get("releasePublisher") or "").strip()
        genre = (row.get("releaseGenre") or "").strip()

        games_by_crc[crc] = {
            "crc32": crc,
            "name": title,
            "noIntroName": no_intro,
            "year": ovg_year,
            "description": ovg_desc,
            "region": region,
            "developer": dev,
            "publisher": pub,
            "genre": genre,
            "players": "",
            "boxart": sanitize_boxart_filename(no_intro),
        }

    # Add any canonical No-Intro ROMs from libretro-database not yet in games_by_crc
    for crc, lr in lr_nointro.items():
        if crc not in games_by_crc:
            no_intro = lr.get("name", "")
            games_by_crc[crc] = {
                "crc32": crc,
                "name": clean_display_title(no_intro),
                "noIntroName": no_intro,
                "year": lr_years_by_crc.get(crc) or lr_years_by_name.get(no_intro, ""),
                "description": "",
                "region": infer_region(no_intro, lr.get("region", "")),
                "developer": "",
                "publisher": "",
                "genre": "",
                "players": "",
                "boxart": sanitize_boxart_filename(no_intro),
            }

    # Enrich every record using ScreenScraper XML, Libretro ReleaseYear, and LaunchBox JSON
    year_count = 0
    desc_count = 0

    for crc, rec in games_by_crc.items():
        no_intro = rec["noIntroName"]
        base_no_intro = re.sub(r"\s*\((?:Rev\s*\w+|Beta[^)]*|Proto[^)]*|Virtual Console|Switch Online|Sample)\)", "", no_intro, flags=re.IGNORECASE).strip()

        ss = ss_by_nointro.get(no_intro) or ss_by_nointro.get(base_no_intro) or {}
        norm_key = normalize_title_key(rec["name"]) or normalize_title_key(no_intro)
        lb = lb_by_norm_title.get(norm_key) or {}

        # Year priority: Libretro -> ScreenScraper -> OpenVGDB -> LaunchBox
        year = (
            lr_years_by_crc.get(crc)
            or lr_years_by_name.get(no_intro)
            or lr_years_by_name.get(base_no_intro)
            or ss.get("year")
            or rec["year"]
            or lb.get("year")
            or "Unknown"
        )
        rec["year"] = year
        if year != "Unknown":
            year_count += 1

        # Description priority:
        # 1. OpenVGDB rich plot synopsis (excluding 1-line auto-summary)
        # 2. ScreenScraper <desc>
        # 3. LaunchBox Overview
        # 4. OpenVGDB 1-line auto-summary
        ovg_desc = rec["description"]
        is_generic_ovg = bool(ovg_desc and GENERIC_DESC_RE.search(ovg_desc))
        if ovg_desc and not is_generic_ovg:
            best_desc = ovg_desc
        elif ss.get("description"):
            best_desc = ss["description"]
        elif lb.get("description"):
            best_desc = lb["description"]
        elif ovg_desc:
            best_desc = ovg_desc
        else:
            best_desc = f"{rec['name']} ({rec['region']}, {rec['year']}) for the Super Nintendo Entertainment System."

        rec["description"] = best_desc
        if best_desc and not best_desc.endswith("for the Super Nintendo Entertainment System."):
            desc_count += 1

        if not rec["developer"]:
            rec["developer"] = ss.get("developer") or lb.get("developer") or ""
        if not rec["publisher"]:
            rec["publisher"] = ss.get("publisher") or lb.get("publisher") or ""
        if not rec["genre"]:
            rec["genre"] = ss.get("genre") or lb.get("genre") or ""
        if not rec["players"]:
            rec["players"] = ss.get("players") or lb.get("players") or "1-2"

    sorted_games = dict(sorted(games_by_crc.items(), key=lambda kv: (kv[1]["name"].lower(), kv[1]["noIntroName"].lower())))

    output_payload = {
        "version": "2.0.0",
        "sources": [
            "OpenVGDB v29.0",
            "ScreenScraper No-Intro (lelexiao)",
            "LaunchBox SNES Metadata (dd900/FE-Metadata)",
            "Libretro Database (No-Intro & ReleaseYear)",
        ],
        "totalGames": len(sorted_games),
        "gamesByCrc32": sorted_games,
    }

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(output_payload, f, ensure_ascii=False, indent=2)

    print(f"[done] Wrote {len(sorted_games)} SNES games to {out_path}")
    print(f"       Valid Release Year: {year_count}/{len(sorted_games)} ({year_count * 100.0 / max(1, len(sorted_games)):.1f}%)")
    print(f"       Curated Description: {desc_count}/{len(sorted_games)} ({desc_count * 100.0 / max(1, len(sorted_games)):.1f}%)")


if __name__ == "__main__":
    main()

