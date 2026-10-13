#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace edgenotes {
struct Note {
  std::uint64_t id = 0;
  std::string title, body, rtf;
  std::uint32_t color = 0;
  bool archived = false;
};
struct Deletion { Note note; std::size_t index; std::uint64_t expires; };
class Store {
 public:
  std::vector<Note> notes;
  std::optional<Deletion> deleted;
  bool erase(std::uint64_t id, std::uint64_t now) {
    auto it = std::find_if(notes.begin(), notes.end(), [id](const Note& n) { return n.id == id; });
    if (it == notes.end()) return false;
    deleted = Deletion{*it, static_cast<std::size_t>(it - notes.begin()), now + 10000};
    notes.erase(it); return true;
  }
  bool undo(std::uint64_t now) {
    if (!deleted) return false;
    if (now >= deleted->expires) { deleted.reset(); return false; }
    notes.insert(notes.begin() + std::min(deleted->index, notes.size()), deleted->note);
    deleted.reset(); return true;
  }
};
inline void put32(std::string& out, std::uint32_t n) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((n >> (i * 8)) & 255));
}
inline void putString(std::string& out, const std::string& s) {
  if (s.size() > 16 * 1024 * 1024) throw std::runtime_error("Note is too large");
  put32(out, static_cast<std::uint32_t>(s.size())); out += s;
}
inline std::string encode(const std::vector<Note>& notes) {
  std::string out = "EDGENOTES1";
  put32(out, static_cast<std::uint32_t>(notes.size()));
  for (const auto& n : notes) {
    put32(out, static_cast<std::uint32_t>(n.id)); put32(out, static_cast<std::uint32_t>(n.id >> 32));
    put32(out, n.color); put32(out, n.archived ? 1 : 0);
    putString(out, n.title); putString(out, n.body); putString(out, n.rtf);
  }
  return out;
}
inline std::vector<Note> decode(const std::string& bytes) {
  if (bytes.size() < 14 || bytes.substr(0, 10) != "EDGENOTES1") throw std::runtime_error("Invalid notes file");
  std::size_t pos = 10;
  auto get32 = [&]() {
    if (pos + 4 > bytes.size()) throw std::runtime_error("Truncated notes file");
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos++])) << (i * 8);
    return v;
  };
  auto getString = [&]() {
    auto size = get32();
    if (size > 16 * 1024 * 1024 || size > bytes.size() - pos) throw std::runtime_error("Invalid note length");
    auto s = bytes.substr(pos, size); pos += size; return s;
  };
  auto count = get32();
  if (count > 100000) throw std::runtime_error("Too many notes");
  std::vector<Note> notes;
  for (std::uint32_t i = 0; i < count; ++i) {
    Note n; auto low = get32(); auto high = get32(); n.id = low | (static_cast<std::uint64_t>(high) << 32);
    n.color = get32() % 8; n.archived = get32() != 0;
    n.title = getString(); n.body = getString(); n.rtf = getString(); notes.push_back(std::move(n));
  }
  if (pos != bytes.size()) throw std::runtime_error("Unexpected file data");
  return notes;
}
}