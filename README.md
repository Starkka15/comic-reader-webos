# Comic Reader for webOS

A comic reader for the HP TouchPad (webOS 3 / webOS Community Edition). It reads the comics on the
TouchPad and downloads more from any server with an OPDS catalog.

- **Formats:** CBZ, CBR (RAR, including RAR5), CB7 and CBT.
- **Little memory, any size of comic:** only the page being read and the next few are ever taken
  out of the archive, and a large scan is scaled to the screen by the system before it is shown.
- **Library:** every comic on the TouchPad as a shelf of covers, with how far each has been read.
  Search, sort by artist, title, recently read or recently added. Hold a cover for more.
- **Reading:** tap the right or left side of the page, or swipe, to turn it; tap the middle for
  the bars. Fit the whole page or its width. A slider jumps through the comic. It opens where you
  left off.
- **Servers:** sign in to an OPDS 1.2 catalog (this project's own server, Komga, Kavita, Calibre
  and others), follow its lists, search it, and download comics to read. Downloads go to
  `comics/<author>/` on the TouchPad and carry on if the app is closed.
- **From other apps:** registered for `.cbz`, `.cbr`, `.cb7` and `.cbt`, so tapping one in a file
  manager opens it here.

## How it is built

An Enyo 1 app (`app/`) with a node service (`service/comics.js`). The service finds the comics,
takes pages out of them with `7za`, and fetches from servers with the system's `curl` (which on
webOS Community Edition speaks current TLS; a sign-in is passed on curl's input, never on a command
line).

`service/bin/7za` is p7zip 16.02 built as one program with every format in it, RAR reading
included; `native/build-7z.sh` downloads the source and builds it. p7zip is LGPL; its RAR code is
under the unRAR license, which allows reading RAR archives and forbids using the code to write
them. Both licenses are in `native/`.

`pdk/` holds the first version of this app, written in C against the webOS PDK. It is not built or
packaged any more: a PDK program can only use the TouchPad's 2011 TLS library, which today's
servers refuse.

## Building

`tools\deploy.ps1` packages, installs and starts the app on a TouchPad attached by USB (Palm SDK
and Java needed; the paths are at the top of the script). `node tools\check.js` checks the code
against what the device's 2011 web engine and node 0.4 understand. `tools\make-icons.ps1` redraws
the icon.

GPL-3.0 for everything but `7za`.
