#!/usr/bin/env bash
# Публикует хвосты логов сборки в ветку ci-logs-<name> (перезаписывается каждый запуск),
# чтобы ошибки компиляции были видны без доступа к интерфейсу Actions.
# usage: push_log.sh <branch-suffix> <logfile>...
set -u
name="$1"; shift
out="$(mktemp -d)"
{
  echo "run: ${GITHUB_RUN_ID:-?}  sha: ${GITHUB_SHA:-?}  job: ${GITHUB_JOB:-?}"
  echo "---- ошибки ----"
  for f in "$@"; do [ -f "$f" ] && case "$f" in *.log) grep -nE "error|Error|FAILED|fatal|undefined|Undefined|CMake Error" "$f" | head -80;; esac; done
} > "$out/SUMMARY.txt"
for f in "$@"; do
  [ -f "$f" ] || continue
  case "$f" in
    *.log) tail -n 700 "$f" > "$out/$(basename "$f")" ;;
    *) cp "$f" "$out/$(basename "$f")" ;;
  esac
done
cd "$out"
git init -q -b "ci-logs-$name"
git add -A
git -c user.name=ci -c user.email=ci@local commit -q -m "logs $name ${GITHUB_RUN_ID:-}"
git push -q --force "https://x-access-token:${GITHUB_TOKEN}@github.com/${GITHUB_REPOSITORY}.git" "HEAD:refs/heads/ci-logs-$name" || true
