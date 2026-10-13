import { useEffect, useState } from "react";
import {
  useEditor,
  EditorContent,
  NodeViewWrapper,
  NodeViewContent,
  ReactNodeViewRenderer,
  type NodeViewProps,
} from "@tiptap/react";
import StarterKit from "@tiptap/starter-kit";
import CodeBlock from "@tiptap/extension-code-block";
import TaskList from "@tiptap/extension-task-list";
import TaskItem from "@tiptap/extension-task-item";
import Placeholder from "@tiptap/extension-placeholder";
import { Markdown } from "@tiptap/markdown";
import {
  Bold,
  Italic,
  Strikethrough,
  Code2,
  List,
  ListTodo,
  Link2,
  Undo2,
  Redo2,
  Copy,
  Check,
  ChevronDown,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import type { Note } from "@/lib/notes";

function CodeSnippet({ node }: NodeViewProps) {
  const [copied, setCopied] = useState(false);
  return (
    <NodeViewWrapper className="code-snippet">
      <div className="code-caption" contentEditable={false}>
        <span>{node.attrs["language"] || "code"}</span>
        <Button
          variant="ghost"
          size="sm"
          aria-label="Copy code"
          onClick={async () => {
            try {
              await navigator.clipboard.writeText(node.textContent);
              setCopied(true);
              setTimeout(() => setCopied(false), 1800);
            } catch {
              setCopied(false);
            }
          }}
        >
          {copied ? <Check /> : <Copy />}
          {copied ? "Copied" : "Copy"}
        </Button>
      </div>
      <pre>
        <NodeViewContent as="div" />
      </pre>
    </NodeViewWrapper>
  );
}
const CopyableCode = CodeBlock.extend({
  addNodeView() {
    return ReactNodeViewRenderer(CodeSnippet);
  },
});

export function NoteEditor({ note, onChange }: { note: Note; onChange: (html: string) => void }) {
  const editor = useEditor({
    extensions: [
      StarterKit.configure({
        codeBlock: false,
        heading: { levels: [1, 2, 3] },
        link: { openOnClick: false },
      }),
      CopyableCode,
      TaskList,
      TaskItem.configure({ nested: true }),
      Placeholder.configure({ placeholder: "A little space for your thoughts…" }),
      Markdown,
    ],
    content: note.content,
    immediatelyRender: false,
    onUpdate: ({ editor }) => onChange(editor.getHTML()),
    editorProps: {
      attributes: { class: "note-prose", "aria-label": "Note content", spellcheck: "true" },
    },
  });
  const [, render] = useState(0);
  useEffect(() => {
    if (!editor) return;
    const update = () => render((n) => n + 1);
    editor.on("selectionUpdate", update);
    editor.on("transaction", update);
    return () => {
      editor.off("selectionUpdate", update);
      editor.off("transaction", update);
    };
  }, [editor]);
  if (!editor) return <div className="editor-loading">Opening your note…</div>;
  const tools = [
    {
      label: "Bold",
      icon: Bold,
      active: editor.isActive("bold"),
      run: () => editor.chain().focus().toggleBold().run(),
    },
    {
      label: "Italic",
      icon: Italic,
      active: editor.isActive("italic"),
      run: () => editor.chain().focus().toggleItalic().run(),
    },
    {
      label: "Strikethrough",
      icon: Strikethrough,
      active: editor.isActive("strike"),
      run: () => editor.chain().focus().toggleStrike().run(),
    },
    {
      label: "Code block",
      icon: Code2,
      active: editor.isActive("codeBlock"),
      run: () => editor.chain().focus().toggleCodeBlock().run(),
    },
    {
      label: "Bullet list",
      icon: List,
      active: editor.isActive("bulletList"),
      run: () => editor.chain().focus().toggleBulletList().run(),
    },
    {
      label: "Checklist",
      icon: ListTodo,
      active: editor.isActive("taskList"),
      run: () => editor.chain().focus().toggleTaskList().run(),
    },
    {
      label: "Add link",
      icon: Link2,
      active: editor.isActive("link"),
      run: () => {
        const href = window.prompt("Link URL", editor.getAttributes("link")["href"] || "https://");
        if (href === null) return;
        if (!href) editor.chain().focus().unsetLink().run();
        else if (/^(https?:\/\/|mailto:)/i.test(href))
          editor.chain().focus().extendMarkRange("link").setLink({ href }).run();
      },
    },
  ];
  return (
    <>
      <div className="format-toolbar">
        <div className="heading-select">
          <select
            aria-label="Text style"
            value={
              editor.isActive("heading", { level: 1 })
                ? "1"
                : editor.isActive("heading", { level: 2 })
                  ? "2"
                  : editor.isActive("heading", { level: 3 })
                    ? "3"
                    : "0"
            }
            onChange={(e) => {
              const level = Number(e.target.value);
              if (level === 1 || level === 2 || level === 3)
                editor.chain().focus().setHeading({ level }).run();
              else editor.chain().focus().setParagraph().run();
            }}
          >
            <option value="0">Normal text</option>
            <option value="1">Heading 1</option>
            <option value="2">Heading 2</option>
            <option value="3">Heading 3</option>
          </select>
          <ChevronDown size={12} />
        </div>
        <span className="toolbar-divider" />
        {tools.map(({ label, icon: Icon, active, run }) => (
          <Button
            key={label}
            variant="ghost"
            size="icon"
            title={label}
            aria-label={label}
            aria-pressed={active}
            className={active ? "tool-active" : ""}
            onClick={run}
          >
            <Icon />
          </Button>
        ))}
        <span className="toolbar-divider" />
        <Button
          variant="ghost"
          size="icon"
          title="Undo"
          aria-label="Undo"
          disabled={!editor.can().undo()}
          onClick={() => editor.chain().focus().undo().run()}
        >
          <Undo2 />
        </Button>
        <Button
          variant="ghost"
          size="icon"
          title="Redo"
          aria-label="Redo"
          disabled={!editor.can().redo()}
          onClick={() => editor.chain().focus().redo().run()}
        >
          <Redo2 />
        </Button>
      </div>
      <EditorContent editor={editor} />
    </>
  );
}
