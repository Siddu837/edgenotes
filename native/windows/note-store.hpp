#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace edgenotes {

enum class BlockKind {
  Plain = 0,
  Heading = 1,
  Bullet = 2,
  Numbered = 3,
  Task = 4
};

enum class NoteFont {
  Handwritten = 0,
  SansSerif = 1,
  Rounded = 2,
  Monospace = 3
};

struct NoteBlock {
  int line = 0;
  int kind = 0;
  bool done = false;
  bool isPlain = true;
};

struct TextSpan {
  int start = 0;
  int length = 0;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool strikethrough = false;
  std::string link;
};

struct Note {
  std::uint64_t id = 0;
  std::string title, body, rtf;
  std::uint32_t color = 0;
  bool archived = false;
  std::string railLabel = "N";
  std::string displayTitle = "";
  int order = 0;
  bool isInDeck = true;
  bool completedTasksAtBottom = true;
  int font = 0;
  std::string updatedUtc = "";

  std::string effectiveRailLabel() const {
    if (!railLabel.empty() && railLabel != "N") return railLabel;
    if (!title.empty()) {
      for (size_t i = 0; i < title.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(title[i]);
        if (c > ' ') {
          if (c >= 'a' && c <= 'z') return std::string(1, static_cast<char>(c - 32));
          if ((c & 0x80) != 0) {
            size_t len = 1;
            if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            if (i + len <= title.size()) return title.substr(i, len);
          }
          return std::string(1, static_cast<char>(c));
        }
      }
    }
    return "N";
  }
};

struct Deletion {
  Note note;
  std::size_t index = 0;
  std::uint64_t expires = 0;
};

class Store {
 public:
  std::vector<Note> notes;
  std::optional<Deletion> deleted;

  bool erase(std::uint64_t id, std::uint64_t now) {
    auto it = std::find_if(notes.begin(), notes.end(), [id](const Note& n) { return n.id == id; });
    if (it == notes.end()) return false;
    deleted = Deletion{*it, static_cast<std::size_t>(it - notes.begin()), now + 10000};
    notes.erase(it);
    return true;
  }

  bool undo(std::uint64_t now) {
    if (!deleted) return false;
    if (now >= deleted->expires) {
      deleted.reset();
      return false;
    }
    notes.insert(notes.begin() + std::min(deleted->index, notes.size()), deleted->note);
    deleted.reset();
    return true;
  }
};

inline void put32(std::string& out, std::uint32_t n) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((n >> (i * 8)) & 255));
}

inline void putString(std::string& out, const std::string& s) {
  if (s.size() > 16 * 1024 * 1024) throw std::runtime_error("Note is too large");
  put32(out, static_cast<std::uint32_t>(s.size()));
  out += s;
}

inline std::string encode(const std::vector<Note>& notes) {
  std::string out = "EDGENOTES1";
  put32(out, static_cast<std::uint32_t>(notes.size()));
  for (const auto& n : notes) {
    put32(out, static_cast<std::uint32_t>(n.id));
    put32(out, static_cast<std::uint32_t>(n.id >> 32));
    put32(out, n.color);
    put32(out, n.archived ? 1 : 0);
    putString(out, n.title);
    putString(out, n.body);
    putString(out, n.rtf);
  }
  return out;
}

inline std::vector<Note> decode(const std::string& bytes) {
  if (bytes.size() < 14 || bytes.substr(0, 10) != "EDGENOTES1") {
    throw std::runtime_error("Invalid notes file");
  }
  std::size_t pos = 10;
  auto get32 = [&]() {
    if (pos + 4 > bytes.size()) throw std::runtime_error("Truncated notes file");
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      v |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos++])) << (i * 8);
    }
    return v;
  };
  auto getString = [&]() {
    auto size = get32();
    if (size > 16 * 1024 * 1024 || size > bytes.size() - pos) {
      throw std::runtime_error("Invalid note length");
    }
    auto s = bytes.substr(pos, size);
    pos += size;
    return s;
  };
  auto count = get32();
  if (count > 100000) throw std::runtime_error("Too many notes");
  std::vector<Note> notes;
  for (std::uint32_t i = 0; i < count; ++i) {
    Note n;
    auto low = get32();
    auto high = get32();
    n.id = low | (static_cast<std::uint64_t>(high) << 32);
    n.color = get32() % 8;
    n.archived = get32() != 0;
    n.title = getString();
    n.body = getString();
    n.rtf = getString();
    notes.push_back(std::move(n));
  }
  if (pos != bytes.size()) throw std::runtime_error("Unexpected file data");
  return notes;
}

inline std::string escapeJsonString(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (char c : s) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\b') out += "\\b";
    else if (c == '\f') out += "\\f";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (static_cast<unsigned char>(c) < 32) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
      out += buf;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

inline std::string encodeJson(const std::vector<Note>& notes) {
  std::string out = "[\n";
  for (std::size_t i = 0; i < notes.size(); ++i) {
    const auto& n = notes[i];
    out += "  {\n";
    out += "    \"id\": " + std::to_string(n.id) + ",\n";
    out += "    \"title\": \"" + escapeJsonString(n.title) + "\",\n";
    out += "    \"body\": \"" + escapeJsonString(n.body) + "\",\n";
    out += "    \"rtf\": \"" + escapeJsonString(n.rtf) + "\",\n";
    out += "    \"colorIndex\": " + std::to_string(n.color % 8) + ",\n";
    out += "    \"railLabel\": \"" + escapeJsonString(n.railLabel.empty() ? n.effectiveRailLabel() : n.railLabel) + "\",\n";
    out += "    \"displayTitle\": \"" + escapeJsonString(n.displayTitle.empty() ? n.title : n.displayTitle) + "\",\n";
    out += "    \"order\": " + std::to_string(n.order) + ",\n";
    out += "    \"isInDeck\": " + std::string(n.isInDeck ? "true" : "false") + ",\n";
    out += "    \"archived\": " + std::string(n.archived ? "true" : "false") + ",\n";
    out += "    \"completedTasksAtBottom\": " + std::string(n.completedTasksAtBottom ? "true" : "false") + ",\n";
    out += "    \"font\": " + std::to_string(n.font) + ",\n";
    out += "    \"updatedUtc\": \"" + escapeJsonString(n.updatedUtc) + "\"\n";
    out += (i + 1 == notes.size()) ? "  }\n" : "  },\n";
  }
  out += "]\n";
  return out;
}

inline std::string unescapeJsonString(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char next = s[++i];
      if (next == '"') out.push_back('"');
      else if (next == '\\') out.push_back('\\');
      else if (next == '/') out.push_back('/');
      else if (next == 'b') out.push_back('\b');
      else if (next == 'f') out.push_back('\f');
      else if (next == 'n') out.push_back('\n');
      else if (next == 'r') out.push_back('\r');
      else if (next == 't') out.push_back('\t');
      else if (next == 'u' && i + 4 < s.size()) {
        try {
          unsigned int cp = std::stoul(s.substr(i + 1, 4), nullptr, 16);
          i += 4;
          if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
          } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
          }
        } catch (...) {
          out.push_back('?');
        }
      } else {
        out.push_back(next);
      }
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

inline std::vector<Note> decodeJson(const std::string& json) {
  std::vector<Note> notes;
  std::size_t pos = 0;
  while ((pos = json.find('{', pos)) != std::string::npos) {
    std::size_t objEnd = json.find('}', pos);
    if (objEnd == std::string::npos) break;
    std::string block = json.substr(pos, objEnd - pos + 1);
    pos = objEnd + 1;

    auto findStringProp = [&](const std::string& key) -> std::string {
      std::string pattern = "\"" + key + "\":";
      auto kpos = block.find(pattern);
      if (kpos == std::string::npos) return "";
      auto q1 = block.find('"', kpos + pattern.size());
      if (q1 == std::string::npos || q1 >= block.size()) return "";
      std::size_t q2 = q1 + 1;
      while (q2 < block.size()) {
        if (block[q2] == '"' && block[q2 - 1] != '\\') break;
        q2++;
      }
      if (q2 >= block.size()) return "";
      return unescapeJsonString(block.substr(q1 + 1, q2 - q1 - 1));
    };

    auto findIntProp = [&](const std::string& key, int defVal) -> int {
      std::string pattern = "\"" + key + "\":";
      auto kpos = block.find(pattern);
      if (kpos == std::string::npos) return defVal;
      auto start = block.find_first_of("-0123456789", kpos + pattern.size());
      if (start == std::string::npos) return defVal;
      try {
        return std::stoi(block.substr(start));
      } catch (...) {
        return defVal;
      }
    };

    auto findU64Prop = [&](const std::string& key, std::uint64_t defVal) -> std::uint64_t {
      std::string pattern = "\"" + key + "\":";
      auto kpos = block.find(pattern);
      if (kpos == std::string::npos) return defVal;
      auto start = block.find_first_of("0123456789", kpos + pattern.size());
      if (start == std::string::npos) return defVal;
      try {
        return std::stoull(block.substr(start));
      } catch (...) {
        return defVal;
      }
    };

    auto findBoolProp = [&](const std::string& key, bool defVal) -> bool {
      std::string pattern = "\"" + key + "\":";
      auto kpos = block.find(pattern);
      if (kpos == std::string::npos) return defVal;
      auto vpos = block.find_first_not_of(" \t\r\n", kpos + pattern.size());
      if (vpos == std::string::npos) return defVal;
      if (block.compare(vpos, 4, "true") == 0) return true;
      if (block.compare(vpos, 5, "false") == 0) return false;
      return defVal;
    };

    Note n;
    n.id = findU64Prop("id", 0);
    n.title = findStringProp("title");
    n.body = findStringProp("body");
    n.rtf = findStringProp("rtf");
    n.color = static_cast<std::uint32_t>(findIntProp("colorIndex", findIntProp("color", 0))) % 8;
    n.railLabel = findStringProp("railLabel");
    n.displayTitle = findStringProp("displayTitle");
    n.order = findIntProp("order", 0);
    n.isInDeck = findBoolProp("isInDeck", true);
    n.archived = findBoolProp("archived", false);
    n.completedTasksAtBottom = findBoolProp("completedTasksAtBottom", true);
    n.font = findIntProp("font", 0);
    n.updatedUtc = findStringProp("updatedUtc");

    if (n.railLabel.empty()) n.railLabel = n.effectiveRailLabel();
    if (n.displayTitle.empty()) n.displayTitle = n.title;

    if (n.id != 0 || !n.title.empty() || !n.body.empty()) {
      notes.push_back(std::move(n));
    }
  }
  return notes;
}

} // namespace edgenotes