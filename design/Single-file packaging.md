# Single-file packaging

Append the asset database to the engine binary itself, producing a single executable file that is the entire game. The player downloads one file, runs it, done.

## How it works

An ELF (Linux), PE (Windows), or Mach-O (macOS) binary ignores data appended after its own end. We exploit this by concatenating the SQLite database after the binary:

```
[ engine binary ][ SQLite database ][ 8-byte size footer ][ 8-byte magic ]
```

The footer stores the size of the SQLite database (as a little-endian uint64), and the magic is a fixed 8-byte sentinel (e.g., `GAMEPAK\0`) so the engine can verify the trailer exists.

## Startup detection

At startup, before any subcommand parsing, the engine:

1. Opens its own executable (`/proc/self/exe` on Linux, `GetModuleFileName` on Windows, `_NSGetExecutablePath` on macOS).
2. Seeks to the last 16 bytes.
3. Checks for the magic sentinel.
4. If found, reads the size footer to get the database offset.
5. Opens the SQLite database from that offset using a custom SQLite VFS (or reads it into memory).

If the sentinel is not found, normal CLI parsing proceeds.

```
file_size = stat(exe_path).size
magic_offset = file_size - 8
size_offset = file_size - 16

magic = read(exe, magic_offset, 8)
if magic != "GAMEPAK\0":
    return NOT_PACKAGED

db_size = read_u64_le(exe, size_offset)
db_offset = file_size - 16 - db_size
db = sqlite3_open_from_offset(exe_path, db_offset, db_size)
```

## SQLite VFS approach

SQLite supports custom VFS implementations. We register a read-only VFS that:

- Opens the engine's own executable file.
- Translates all read offsets by adding `db_offset`.
- Reports the database size as `db_size`.
- Rejects all write operations (the database is read-only in packaged mode).

This avoids loading the entire database into memory, which matters for games with large assets.

```c
static int xRead(sqlite3_file* file, void* buf, int amt, sqlite3_int64 offset) {
    // Translate offset into the appended region
    return pread(fd, buf, amt, offset + db_offset);
}

static int xFileSize(sqlite3_file* file, sqlite3_int64* size) {
    *size = db_size;
    return SQLITE_OK;
}
```

SQLite's `sqlite3_vfs_register` and `sqlite3_open_v2` with the custom VFS name make this straightforward.

## Packaging command

`game package --single-file` (or a separate flag) produces a single file:

```bash
game package my-game --single-file -o dist/
# produces: dist/my-game (single executable, ~= binary + assets)
```

Implementation:

```bash
# Conceptually:
cp game dist/my-game
cat assets.sqlite3 >> dist/my-game
printf '%016x' $(stat -c%s assets.sqlite3) | xxd -r -p >> dist/my-game
printf 'GAMEPAK\0' >> dist/my-game
```

In practice this is done in C++ with file I/O.

## Size considerations

The packaged file size = engine binary + SQLite database. For a typical small game:

- Engine binary: ~5-15 MB (depending on static vs dynamic linking, strip)
- Assets database: varies (a few MB for a small game, 50+ MB for a larger one)
- Total: ~10-70 MB for most indie games

This is comparable to other single-file game engines (Love2D `.love` files are similar but require a separate runtime).

## Limitations

- **Read-only**: The packaged game cannot modify its own asset database. Save data must go through PhysFS write directory (which already works this way).
- **Code signing**: On macOS, appending data to a signed binary invalidates the signature. The binary must be re-signed after packaging (or not signed at all for indie distribution).
- **Anti-virus**: Some Windows AV software flags modified executables. This is a known issue with the append-to-binary technique. Code signing with a trusted certificate mitigates this.
- **Shared libraries**: Single-file mode only embeds assets, not shared libraries. If the engine depends on SDL2.dll, that still needs to be distributed alongside. Static linking eliminates this issue.

## Alternative: Memory-mapped approach

Instead of a custom SQLite VFS, the engine could memory-map the appended region and use `sqlite3_deserialize` to open it as an in-memory database. This is simpler but loads the entire database into virtual memory (though the OS pages it in on demand, so physical memory usage is similar).

```c
void* db_mem = mmap(NULL, db_size, PROT_READ, MAP_PRIVATE, fd, db_offset);
sqlite3_open(":memory:", &db);
sqlite3_deserialize(db, "main", db_mem, db_size, db_size, SQLITE_DESERIALIZE_READONLY);
```

This is simpler to implement and a good starting point. The custom VFS can be an optimization if needed.
