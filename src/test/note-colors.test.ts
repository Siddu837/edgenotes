import { describe, expect, it } from "vitest";
import { noteColor, noteColors, type Note } from "@/lib/notes";

const note: Note = { id: "test-note", deck: "work", title: "My note", content: "<p></p>", tags: [], updated: "2026-10-13T00:00:00Z" };

describe("Pastel paper colors", () => {
  it("preserves older notes without a paper color", () => {
    expect(noteColor(note)).toBe("mint");
    expect(noteColor({ ...note, deck: "personal" })).toBe("sky");
  });
  it("uses selected colors and rejects unknown stored values", () => {
    for (const color of noteColors) expect(noteColor({ ...note, color })).toBe(color);
    expect(noteColor({ ...note, color: "invalid" })).toBe("mint");
  });
});