<!-- translation-source: docs/reports/documentation-audit-2026-10-31.md; sha256: 5b8954c4cb521ea6024d088857868bd6fb8974e46faab333d8039a26bdf5059d -->
*[Français](documentation-audit-2026-10-31.md) | [English](documentation-audit-2026-10-31_en.md)*

# Documentation — reconciliation and translation of live documents

Date: 31 October 2026. Branch: `patch_documentation`, dedicated worktree.
**Local** production reference: `dev` `b19b4c3`. This pass checks neither
remote CI nor all-platform certification.

## 1. Requested scope and limits

The user requests current documentation and missing translations, prioritizing
**live documents**(manuals, architecture, decisions, protocols). Historical
archives are lower priority. Measurement JSON is neither translated nor rewritten.

**This batch does not complete the entire request.** Remaining references
are inventoried; an English file's presence proves neither currency nor
quality. Old dates/validation reservations do not become 31 October validations
through review or translation alone.

## 2. Synchronization and French corrections

Documentation had six own commits and was 198 commits behind `dev`.
Local merge `a9a5d4d`, without squash/rebase or changing `dev`. Conflicts were
reconciled preserving presentation corrections and newer validated engine
states, not by blindly selecting one documentation side.

Main corrections:

- user guide: connected commands, destination as-shot WB on paste, batch
  export, Global grading/blending/balance, master noise control and experimental
  signed mode;
- noise architecture: classic versus pre-LCP/DCP paths, session state,
  disabled default, validation limits and thread ceiling;
- ADR-0002/0005: identify historical sRGB/mixer contracts and analyzer
  switches rather than advertise them as current instructions;
- ADR-0012/0018 and decoder roadmap: floating development already integrated,
  LibRaw decompression/metadata retained, Canon priority and other-brand LibRaw
  retention, awaiting Canon Developer response;
- matrix: update Noise without claiming universal quality; bench variants
  unpromoted and incremental research suspended;
- French technical manual: record floating development without claiming a
  new codec;
- course4: distinguish historical teaching mixer from current response tables;
  rebuild the associated PDF.

The beta roadmap and current RAW report were imported from my committed
`patch_raw_bench` work, with 74 already-committed new JSON evidence files.
Bench tools and engine changes from that branch were not imported. Results
remain studies, not engine delivery. Other agents' uncommitted work was not copied.

## 3. Translations completed in this batch

- complete user guide: LaTeX root and 21 chapters, including shortcuts;
  three future families remain reserved as in French;
- guide README and dual-language build(`-Language fr|en|all`);
- all 20 ADRs and their English index;
- beta roadmap;
- common import and memory-control contracts;
- refreshed noise architecture, Architecture index and documentation index;
- this bilingual review report.

Links to untranslated documents remain French rather than target imaginary
equivalents. Existing English technical chapters still need reconciliation:
their README explicitly warns about the old sRGB master/calibration guards.

## 4. Inventory and drift prevention

`python scripts/audit_translations.py` separates missing, present/unverified,
changed source, wrong source and matching source hash. Hashes cover normalized
UTF-8/LF French text, independent of Windows/Linux Git line endings. They
do not replace meaning checks.

Versioned [v001 inventory](documentation-translation-inventory-v001-20261031.json)
records paths, sizes, French references and scopes; archives, analysis reports
and courses remain distinct from live references. AGENTS.md and generated
build trees are excluded, not declared translated.

## 5. Checks and unfinished work

- three inventory tests: statuses, wrong source and CRLF/LF neutrality;
- FR/EN user guides compile to 49 pages each, 12pt/1.5 line spacing;
- no overflow or undefined reference found in final logs;
- targeted visual inspection of two English pages including shortcuts,
  not exhaustive every-page review;
- French technical manual/course4 rebuilt;
- UTF-8/JSON/local-link checks and `git diff --check`; not exhaustive heading
  anchor or external-link validation.

Next priorities before claiming all live documents current:

1. Reconcile English technical chapters/diagrams with current French, then
   compile/review the English PDF.
2. Translate remaining long references: profile engine, pipeline, roadmaps
   and specifications; compare decoding assumptions with current code.
3. Translate/verify protocols and analyzer/generator guides.
4. Review other existing English build/CI/validation READMEs; “present”
   does not mean “reviewed”.
5. Then address courses/reports according to user priority; archives last.

No photographic code or corpus image modified. No push, merge into `dev`/
`main`, tag, photographic campaign or new rendering validation is claimed.
