# To do

## Checked on the TouchPad (2026-10-10)

By screenshot: the library with covers made from a CBZ and a CBR; the reader showing a page of
each; the server's first list and a list of comics with covers, signed in to an OPDS catalog over
HTTPS. Through the service from a shell: a 4 MB download with progress, then opening it and taking
out a page; a wrong password refused with a plain message. Through the system: `.cbz` and `.cbr`
are registered to this app, and opening a CBR by its path starts the reader on it.

## Not yet tried by hand

Everything that takes a finger: turning pages by tap and swipe, the slider, Fit Width and its
scrolling, the bars hiding, holding a cover and its menu (start over, mark unread, delete), sort
and search in the library, typing a sign-in, following the server's lists and its Back, searching
the server, the comic's details and Download, the download strip and Cancel, "On this TouchPad"
appearing under a downloaded comic, portrait.

## Known gaps

- No pinch zoom: Fit Page and Fit Width only.
- No double-page spread handling and no right-to-left reading.
- Reading progress is kept on the TouchPad only; the server's own read marks are not updated
  (OPDS has no standard way; this server's other APIs could be used for it).
- A comic can only be read after it has been downloaded whole; there is no reading straight from
  the server.
- One download at a time; the others wait their turn in the app, and that waiting list is lost if
  the app is closed (the one in progress carries on).
- A CBR made as one "solid" block gets slower to open pages from the further in you go.
- Page names with characters outside plain English have not been tried.
- The password is kept in the app's settings unencrypted.
- The first version's Nextcloud (WebDAV) browsing is not in this one.
