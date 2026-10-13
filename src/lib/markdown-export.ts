import TurndownService from "turndown";

export function toMarkdown(html: string): string {
  const converter = new TurndownService({
    headingStyle: "atx",
    codeBlockStyle: "fenced",
    bulletListMarker: "-",
  });
  converter.addRule("checklist", {
    filter: (node) => node.nodeName === "LI" && node.getAttribute("data-type") === "taskItem",
    replacement: (content, node) =>
      `\n- [${node.getAttribute("data-checked") === "true" ? "x" : " "}] ${content.trim()}\n`,
  });
  converter.addRule("strike", { filter: ["s", "del"], replacement: (content) => `~~${content}~~` });
  return converter.turndown(html);
}
