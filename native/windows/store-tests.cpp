#include "note-store.hpp"
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed: " #x "\n"; return 1; } } while (false)
int main() {
  using namespace edgenotes;
  Store s; s.notes = {{1, "A UTF-8 thought \xe2\x9c\xa8", "Line one\nLine two", "{\\rtf1 hello}", 2, false}, {2, "Second", "body", "", 5, true}};
  auto bytes = encode(s.notes); auto restored = decode(bytes);
  CHECK(restored.size() == 2); CHECK(restored[0].title == s.notes[0].title);
  CHECK(restored[0].rtf == s.notes[0].rtf); CHECK(restored[1].archived);
  CHECK(s.erase(1, 100)); CHECK(s.notes.size() == 1); CHECK(s.undo(10099)); CHECK(s.notes[0].id == 1);
  CHECK(s.erase(1, 200)); CHECK(!s.undo(10200)); CHECK(s.notes.size() == 1);
  CHECK(!s.erase(999, 0));
  bool rejected = false; try { decode(bytes.substr(0, bytes.size() - 1)); } catch (...) { rejected = true; }
  CHECK(rejected);
  rejected = false; try { decode("not a notes file"); } catch (...) { rejected = true; }
  CHECK(rejected);
  std::cout << "Native note store checks passed\n";
  return 0;
}