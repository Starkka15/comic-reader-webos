# Comic Reader for webOS

A memory-efficient comic book reader for HP TouchPad (webOS PDK).

## Features

- **CBZ Support**: Reads CBZ/ZIP comic archives
- **Memory Efficient**: Only keeps 3 pages in memory at a time (LRU cache)
- **Auto-Scaling**: Images scaled to screen resolution on load
- **Touch Navigation**: Swipe or tap to turn pages
- **File Browser**: Navigate to your comics folder
- **Natural Sorting**: Pages sorted correctly (1, 2, 10 not 1, 10, 2)

## Memory Management

Unlike other readers that crash on large files, this reader:
- Reads ZIP central directory only (not full extraction)
- Extracts single pages on demand
- Scales images to screen size immediately (discards full resolution)
- LRU cache evicts old pages automatically
- Typical memory usage: ~15MB for any size comic

## Building

```bash
export WEBOS_PDK=/opt/PalmPDK
make
palm-package .
palm-install org.webos.comicreader_*.ipk
```

## Usage

1. Place CBZ files in `/media/internal/comics/` (or browse to any folder)
2. Launch "Comic Reader"
3. Tap a comic to open
4. Navigation:
   - Tap left third of screen: Previous page
   - Tap right third of screen: Next page
   - Swipe left/right: Turn pages
   - Tap [Back] in bottom bar: Return to browser

## Supported Formats

- **CBZ** (ZIP archives with images) - Full support
- **CBR** (RAR archives) - Not yet supported

## Technical Details

- Uses minizip for ZIP reading (bundled)
- SDL_image for JPEG/PNG decoding
- Screen: 1024x768 (TouchPad native)
- Cache: 3 pages (prev, current, next)

## License

GPL-3.0
