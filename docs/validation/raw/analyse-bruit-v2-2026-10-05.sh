#!/usr/bin/env bash
set -euo pipefail
# Run from repository root. No photos are changed. Explicit campaign mappings.
# Usage: bash THIS_SCRIPT old|new|all OUTPUT.json [--analyse [--previews DIR]]
part="${1:?expected old, new or all}"
output="${2:?expected a new output JSON path}"
shift 2
old=(6V1A0043 6V1A0112 6V1A0327 6V1A0624 6V1A9890)
# Actual IDs confirmed by the user: 9950/1020, not the announced 1050/9920.
new=(6V1A9950 6V1A0106 6V1A1020 6V1A9734 6V1A9421 6V1A9377 6V1A8988)
case "$part" in
  old) ids=("${old[@]}");;
  new) ids=("${new[@]}");;
  all) ids=("${old[@]}" "${new[@]}");;
  *) echo 'expected old, new or all' >&2; exit 2;;
esac
args=(--root tests/image/base --image-id "${ids[@]}" --output "$output")
for id in "${ids[@]}"; do
  case "$id" in
    6V1A0043|6V1A0112|6V1A0327|6V1A0624|6V1A9890)
      for component in luminance couleur; do
        for number in 1 2 3 4; do
          suffix=$((number + 4))
          args+=(--variant-file "$id" "$component" IPR "$number"
            "tests/image/base/bruit/$component/$id-IPR-$suffix.tiff")
        done
      done;;
  esac
  if [[ "$id" == 6V1A0112 ]]; then
    args+=(--neutral-file "$id" IPR tests/image/base/neutre/6V1A0112-IPR-5.tiff)
  fi
  if [[ "$id" == 6V1A9890 ]]; then
    for number in 1 2 3 4; do
      suffix=$((number + 4))
      args+=(--variant-file "$id" luminance LR "$number"
        "tests/image/base/bruit/luminance/$id-$suffix.tif")
    done
  fi
  for value in 1 5 10; do
    printf -v padded '%03d' "$value"
    extra="tests/image/base/bruit/luminance/$id-IPR-L$padded.tiff"
    if [[ -f "$extra" ]]; then
      args+=(--extra-state "$id" luminance IPR "$value" "$extra")
    fi
  done
done
python3 tests/analyse_noise.py "${args[@]}" "$@"
