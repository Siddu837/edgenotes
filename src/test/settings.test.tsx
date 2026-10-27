import { describe, expect, it, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import { SettingsDialog } from "@/components/settings-dialog";

describe("SettingsDialog", () => {
  it("renders all setting sections and options when open", () => {
    const onDefaultFontChange = vi.fn();
    const onDarkToggle = vi.fn();
    const onSideChange = vi.fn();
    const onCompletedTasksAtBottomChange = vi.fn();
    const onClose = vi.fn();

    render(
      <SettingsDialog
        open={true}
        onClose={onClose}
        defaultFont="handwritten"
        onDefaultFontChange={onDefaultFontChange}
        dark={false}
        onDarkToggle={onDarkToggle}
        side="right"
        onSideChange={onSideChange}
        completedTasksAtBottom={true}
        onCompletedTasksAtBottomChange={onCompletedTasksAtBottomChange}
      />,
    );

    expect(screen.getByText("EdgeNotes Settings")).toBeInTheDocument();
    expect(screen.getByText("Default Note Font")).toBeInTheDocument();
    expect(screen.getByText("Appearance & Theme")).toBeInTheDocument();
    expect(screen.getByText("Dock Edge Position")).toBeInTheDocument();
    expect(screen.getByText("Completed Checklist Items")).toBeInTheDocument();

    // Verify font options
    expect(screen.getByText("Handwritten")).toBeInTheDocument();
    expect(screen.getByText("Sans-Serif")).toBeInTheDocument();
    expect(screen.getByText("Rounded")).toBeInTheDocument();
    expect(screen.getByText("Monospace")).toBeInTheDocument();

    // Change font
    fireEvent.click(screen.getByText("Sans-Serif"));
    expect(onDefaultFontChange).toHaveBeenCalledWith("sans");

    // Change dock position
    fireEvent.click(screen.getByText("Left Edge"));
    expect(onSideChange).toHaveBeenCalledWith("left");

    // Close
    fireEvent.click(screen.getByText("Done"));
    expect(onClose).toHaveBeenCalled();
  });

  it("does not render when closed", () => {
    const { container } = render(
      <SettingsDialog
        open={false}
        onClose={() => {}}
        defaultFont="handwritten"
        onDefaultFontChange={() => {}}
        dark={false}
        onDarkToggle={() => {}}
        side="right"
        onSideChange={() => {}}
        completedTasksAtBottom={true}
        onCompletedTasksAtBottomChange={() => {}}
      />,
    );
    expect(container).toBeEmptyDOMElement();
  });
});
