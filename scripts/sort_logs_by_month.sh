#!/usr/bin/env bash
set -euo pipefail

dir="/home/general/SignalAssistant/logs"
apply=false

display_help(){
  cat <<EOF
Usage: $0 [--dir DIR] [--apply] [--dry-run]
  --dir DIR    Directory containing log files (default: /home/general/SignalAssistant/logs)
  --apply      Actually move files. Without this flag runs a dry-run.
  --dry-run    Explicit dry-run (default)
EOF
}

# parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dir)
      dir="$2"; shift 2;;
    --apply)
      apply=true; shift;;
    --dry-run)
      apply=false; shift;;
    -h|--help)
      display_help; exit 0;;
    *)
      echo "Unknown arg: $1" >&2; display_help; exit 2;;
  esac
done

if [[ ! -d "$dir" ]]; then
  echo "Directory not found: $dir" >&2
  exit 1
fi

shopt -s nullglob
for f in "$dir"/*; do
  # skip directories
  if [[ -d "$f" ]]; then
    continue
  fi
  base=$(basename "$f")
  folder=""
  if [[ $base =~ ^session-([0-9]{4})([0-9]{2})([0-9]{2})-.*\.log$ ]]; then
    year=${BASH_REMATCH[1]}
    month=${BASH_REMATCH[2]}
    folder="$dir/$month-$year"
  else
    # fallback to file mtime
    if stat_out=$(stat -c %Y "$f" 2>/dev/null); then
      month=$(date -d "@${stat_out}" +%m)
      year=$(date -d "@${stat_out}" +%Y)
      folder="$dir/$month-$year"
    else
      echo "Unable to determine date for $base; skipping" >&2
      continue
    fi
  fi

  if [[ "$apply" == true ]]; then
    mkdir -p "$folder"
    mv -v -- "$f" "$folder/"
  else
    echo "Would move $base -> $(basename "$folder")/" 
  fi
done
