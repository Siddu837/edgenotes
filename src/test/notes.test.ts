import { describe, expect, it } from "vitest";
import { initialNotes, removeLegacySamples, plainText, type Note } from "@/lib/notes";
import { toMarkdown } from "@/lib/markdown-export";

describe("Note export", () => {
  it("starts empty and removes only legacy samples from saved notes", () => {
    const real: Note = { id: "user-created-uuid", deck: "work", title: "Website refresh", content: "<p>My own content</p>", tags: [], updated: "2026-10-13T00:00:00Z" };
    expect(initialNotes).toEqual([]);
    expect(removeLegacySamples([{ ...real, id: "website" }, real])).toEqual([real]);
  });
  it("preserves headings, links, formatting, code, and checklists", () => {
    const result = toMarkdown(
      '<h2>Plan</h2><p><strong>Important</strong> and <s>done</s> <a href="https://example.com">link</a></p><ul data-type="taskList"><li data-type="taskItem" data-checked="true"><p>Finish</p></li><li data-type="taskItem" data-checked="false"><p>Review</p></li></ul><pre><code>const x = 1;</code></pre>',
    );
    expect(result).toContain("## Plan");
    expect(result).toContain("**Important**");
    expect(result).toContain("~~done~~");
    expect(result).toContain("[link](https://example.com)");
    expect(result).toContain("- [x] Finish");
    expect(result).toContain("- [ ] Review");
    expect(result).toContain("```");
  });
  it("extracts readable body text for search and word counts", () => {
    expect(plainText("<p>Design &amp; ideas</p><h2>Next</h2>")).toBe("Design & ideas\nNext");
  });
});
