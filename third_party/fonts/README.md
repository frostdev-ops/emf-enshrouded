# Bundled fonts

EMF's on-screen text and console use these fonts, bundled unmodified. Both are under the SIL Open Font License 1.1;
the licence text is next to each font and ships in the release zip's `licenses\`. The build copies the fonts to
`dist\emf\fonts\`, and they install to `<game>\emf\fonts\` (where a player can also drop a font of their own for
`[hud] font=file:<name>.ttf`).

| Font | Version | From | Files | SHA-256 |
|---|---|---|---|---|
| Alegreya by The Alegreya Project Authors (Huerta Tipográfica) | 2.008 | [github.com/huertatipografica/Alegreya](https://github.com/huertatipografica/Alegreya), tag `v2.008`, `fonts/ttf/` of https://github.com/huertatipografica/Alegreya/archive/refs/tags/v2.008.zip | `alegreya/Alegreya-Regular.ttf` (327,784 bytes) | `1065bc1fcfd1da0c1a18ddec81537dfdf06749db1ca3ae4d6dc40244058a6036` |
| | | | `alegreya/Alegreya-Bold.ttf` (335,920 bytes) | `ba5f7c026c7677a879442ffda746aaf544e7b8e1cb394f476df80147986d1684` |
| | | | `alegreya/OFL.txt` (the licence, from the same archive) | |
| Cascadia Mono by Microsoft | 2407.24 | [github.com/microsoft/cascadia-code](https://github.com/microsoft/cascadia-code), release `v2407.24`, `ttf/static/` of https://github.com/microsoft/cascadia-code/releases/download/v2407.24/CascadiaCode-2407.24.zip | `cascadia/CascadiaMono-Regular.ttf` (575,912 bytes) | `06520d032ec274fa5040b22c6f4a1d829081b24ba40b2da56dae89bf10c7b481` |
| | | | `cascadia/LICENSE` (the licence, the repository's `LICENSE` at tag `v2407.24`; the release zip has none) | |

Each font's name table says its version: "Version 2.008" (Alegreya) and "Version 2407.024" (Cascadia Mono).

Consolas, the console's default font, is Windows' own (`C:\Windows\Fonts\consola.ttf`): EMF loads it when it is there
and never ships it.

To update a font: download it from the project's own release, check its licence, replace the file unmodified, and
update the version, the URL and the SHA-256 here.
