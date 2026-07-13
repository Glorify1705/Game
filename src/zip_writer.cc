#include "zip_writer.h"

#include <cerrno>
#include <cstring>

#include "logging.h"

namespace G {

namespace {

// Zip format signatures and fixed header sizes (without filename).
constexpr uint32_t kLocalFileHeaderSig = 0x04034b50;
constexpr uint32_t kCentralDirSig = 0x02014b50;
constexpr uint32_t kEndOfCentralDirSig = 0x06054b50;
constexpr size_t kLocalFileHeaderSize = 30;
constexpr size_t kCentralDirEntrySize = 46;
constexpr size_t kEndOfCentralDirSize = 22;
// "Version needed to extract" 2.0: plain STORE entries.
constexpr uint16_t kZipVersion = 20;
constexpr size_t kMaxEntries = 65535;

struct Crc32Table {
  uint32_t entries[256];

  constexpr Crc32Table() : entries{} {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int bit = 0; bit < 8; ++bit) {
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      entries[i] = c;
    }
  }
};

constexpr Crc32Table kCrc32Table;

void PutU16(uint8_t* out, uint16_t v) {
  out[0] = static_cast<uint8_t>(v);
  out[1] = static_cast<uint8_t>(v >> 8);
}

void PutU32(uint8_t* out, uint32_t v) {
  out[0] = static_cast<uint8_t>(v);
  out[1] = static_cast<uint8_t>(v >> 8);
  out[2] = static_cast<uint8_t>(v >> 16);
  out[3] = static_cast<uint8_t>(v >> 24);
}

ErrorOr<void> WriteAll(FILE* file, const void* data, size_t size) {
  if (size == 0) return {};
  if (fwrite(data, 1, size, file) != size) {
    return Error::Message("failed to write zip archive data");
  }
  return {};
}

}  // namespace

uint32_t Crc32(ByteSlice data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < data.size(); ++i) {
    crc = kCrc32Table.entries[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

ZipWriter::~ZipWriter() {
  CHECK(file_ == nullptr, "ZipWriter destroyed without calling Finish()");
}

ErrorOr<void> ZipWriter::Open(const char* path) {
  CHECK(file_ == nullptr, "ZipWriter::Open called twice");
  file_ = fopen(path, "wb");
  if (file_ == nullptr) return Error::Errno(errno);
  return {};
}

ErrorOr<void> ZipWriter::AddEntry(std::string_view name, ByteSlice contents) {
  CHECK(file_ != nullptr, "ZipWriter::AddEntry before Open");
  CHECK(entries_.size() < kMaxEntries, "too many zip entries (max 65535)");

  Entry entry;
  CHECK(name.size() < sizeof(entry.name), "zip entry name too long: ", name);
  std::memcpy(entry.name, name.data(), name.size());
  entry.name[name.size()] = '\0';
  entry.name_len = static_cast<uint16_t>(name.size());
  entry.crc32 = Crc32(contents);
  CHECK(contents.size() <= UINT32_MAX,
        "zip entry exceeds 4 GiB (zip64 unsupported): ", name);
  entry.size = static_cast<uint32_t>(contents.size());

  const long pos = ftell(file_);
  if (pos < 0) return Error::Errno(errno);
  CHECK(static_cast<uint64_t>(pos) + kLocalFileHeaderSize + name.size() +
                contents.size() <=
            UINT32_MAX,
        "zip archive exceeds 4 GiB (zip64 unsupported)");
  entry.offset = static_cast<uint32_t>(pos);

  uint8_t header[kLocalFileHeaderSize];
  PutU32(header + 0, kLocalFileHeaderSig);
  PutU16(header + 4, kZipVersion);      // version needed to extract
  PutU16(header + 6, 0);                // general purpose flags
  PutU16(header + 8, 0);                // compression method: STORE
  PutU16(header + 10, 0);               // DOS time
  PutU16(header + 12, 0);               // DOS date
  PutU32(header + 14, entry.crc32);     // crc-32
  PutU32(header + 18, entry.size);      // compressed size
  PutU32(header + 22, entry.size);      // uncompressed size
  PutU16(header + 26, entry.name_len);  // filename length
  PutU16(header + 28, 0);               // extra field length
  TRY(WriteAll(file_, header, sizeof(header)));
  TRY(WriteAll(file_, name.data(), name.size()));
  TRY(WriteAll(file_, contents.data(), contents.size()));

  entries_.Push(entry);
  return {};
}

ErrorOr<void> ZipWriter::Finish() {
  CHECK(file_ != nullptr, "ZipWriter::Finish before Open");

  const long central_dir_pos = ftell(file_);
  if (central_dir_pos < 0) return Error::Errno(errno);

  for (size_t i = 0; i < entries_.size(); ++i) {
    const Entry& entry = entries_[i];
    uint8_t header[kCentralDirEntrySize];
    PutU32(header + 0, kCentralDirSig);
    PutU16(header + 4, kZipVersion);      // version made by
    PutU16(header + 6, kZipVersion);      // version needed to extract
    PutU16(header + 8, 0);                // general purpose flags
    PutU16(header + 10, 0);               // compression method: STORE
    PutU16(header + 12, 0);               // DOS time
    PutU16(header + 14, 0);               // DOS date
    PutU32(header + 16, entry.crc32);     // crc-32
    PutU32(header + 20, entry.size);      // compressed size
    PutU32(header + 24, entry.size);      // uncompressed size
    PutU16(header + 28, entry.name_len);  // filename length
    PutU16(header + 30, 0);               // extra field length
    PutU16(header + 32, 0);               // comment length
    PutU16(header + 34, 0);               // disk number start
    PutU16(header + 36, 0);               // internal attributes
    PutU32(header + 38, 0);               // external attributes
    PutU32(header + 42, entry.offset);    // local header offset
    TRY(WriteAll(file_, header, sizeof(header)));
    TRY(WriteAll(file_, entry.name, entry.name_len));
  }

  const long end_pos = ftell(file_);
  if (end_pos < 0) return Error::Errno(errno);
  const uint32_t central_dir_size =
      static_cast<uint32_t>(end_pos - central_dir_pos);
  const uint16_t count = static_cast<uint16_t>(entries_.size());

  uint8_t eocd[kEndOfCentralDirSize];
  PutU32(eocd + 0, kEndOfCentralDirSig);
  PutU16(eocd + 4, 0);       // disk number
  PutU16(eocd + 6, 0);       // central directory start disk
  PutU16(eocd + 8, count);   // entries on this disk
  PutU16(eocd + 10, count);  // total entries
  PutU32(eocd + 12, central_dir_size);
  PutU32(eocd + 16, static_cast<uint32_t>(central_dir_pos));
  PutU16(eocd + 20, 0);  // comment length
  TRY(WriteAll(file_, eocd, sizeof(eocd)));

  if (fclose(file_) != 0) {
    file_ = nullptr;
    return Error::Errno(errno);
  }
  file_ = nullptr;
  return {};
}

}  // namespace G
