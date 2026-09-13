# Noto Sans SC Web Fonts

Self-hosted variable WOFF2 subsets derived from the official Noto Sans SC
**2.004** release. Every Unicode codepoint in the upstream font is retained
across disjoint CSS `unicode-range` subsets. This is not a fixed-copy subset:
user messages can use all 30,890 supported characters.

Source:

- Project: https://github.com/notofonts/noto-cjk
- Tag: `Sans2.004`
- File: `Sans/Variable/TTF/Subset/NotoSansSC-VF.ttf`
- Source URL: https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans/Variable/TTF/Subset/NotoSansSC-VF.ttf
- Git blob SHA-1: `5371a543be5fc670c7cdee9760c03554ee3e9b8e`
- License: [SIL Open Font License 1.1](OFL.txt)
- Retrieved: 2026-09-12

The font is copyright Adobe, with reserved name `Source`. The original
copyright and licensing metadata are retained in each font file. The build
changes the web container and character partition only, preserving the
variable weight axis from 100 to 900 and the font's layout features.

## Loading

Load `fonts.css` before the application stylesheet. Set the UI font family
to `"Noto Sans SC", "Microsoft YaHei UI", sans-serif`.
Latin and common Chinese use separate subsets; four common subsets contain
GB2312 level-one characters plus CJK punctuation and full-width forms,
with at most 1,024 codepoints per file to stay below the server's output limit.
Remaining characters load in 512-codepoint subsets as needed. Every face
tries `local("Noto Sans SC")` first and uses `font-display: swap`, avoiding
network requests when that font is already installed.

Use `font-weight` for continuous weights, not a fixed `font-variation-settings`
override. The same family covers Latin and Chinese for consistent metrics.
The server must send `.woff2` as `font/woff2` and `.css` as `text/css`.

## Rebuilding

Use Python with `fonttools==4.65.0` and `brotli==1.2.0` in an isolated
temporary environment. Run:

```sh
python build_subsets.py /path/to/NotoSansSC-VF.ttf
```

The builder verifies the exact source blob, checks disjoint/full Unicode
coverage and retained weight axes, and emits `manifest.json` with file sizes
and Unicode ranges. No font build dependency is needed to run the app.

## Verified Loading Budget

The 57 WOFF2 files total 9,782,620 bytes; the largest is 311,716 bytes.
On the actual application server, with local fonts disabled in Chromium,
the login screen requests only Latin and four common subsets: 1,196,372
bytes total (1.14 MiB). A locally installed Noto Sans SC needs no font
downloads. Adding U+9F98 plus mixed Chinese/Latin text fetches one additional
171,868-byte subset and renders entirely in the custom Noto family.

Verification used Chromium's `CSS.getPlatformFontsForNode` to distinguish
actual font usage from the CSS fallback list. All requested WOFF2 files
returned `font/woff2`, with no font-decoding or console errors. The browser
check and full source-coverage/weight/license checks passed on 2026-09-12.
