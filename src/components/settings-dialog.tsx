import React from "react";
import {
  X,
  Type,
  Sun,
  Moon,
  ArrowLeft,
  ArrowRight,
  ArrowUp,
  ArrowDown,
  CheckCircle2,
  ListTodo,
  Sparkles,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { fontOptions, type NoteFont } from "@/lib/notes";
import type { DockSide } from "@/lib/dock-position";

export interface SettingsDialogProps {
  open: boolean;
  onClose: () => void;
  defaultFont: NoteFont;
  onDefaultFontChange: (font: NoteFont) => void;
  dark: boolean;
  onDarkToggle: (dark: boolean) => void;
  side: DockSide;
  onSideChange: (side: DockSide) => void;
  completedTasksAtBottom: boolean;
  onCompletedTasksAtBottomChange: (val: boolean) => void;
}

export function SettingsDialog({
  open,
  onClose,
  defaultFont,
  onDefaultFontChange,
  dark,
  onDarkToggle,
  side,
  onSideChange,
  completedTasksAtBottom,
  onCompletedTasksAtBottomChange,
}: SettingsDialogProps) {
  if (!open) return null;

  return (
    <div
      className="manager-overlay dark settings-overlay"
      role="dialog"
      aria-modal="true"
      aria-label="Settings"
      onPointerDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
      onKeyDown={(e) => {
        if (e.key === "Escape") {
          e.stopPropagation();
          onClose();
        }
      }}
    >
      <div className="settings-modal" role="document">
        <header className="settings-header">
          <div className="settings-header-title">
            <Sparkles size={18} className="text-primary" />
            <div>
              <h2>EdgeNotes Settings</h2>
              <span>Customize your workspace, typography, and edge dock</span>
            </div>
          </div>
          <Button
            variant="ghost"
            size="icon"
            className="settings-close-btn"
            aria-label="Close settings"
            onClick={onClose}
          >
            <X size={16} />
          </Button>
        </header>

        <div className="settings-body">
          {/* Section: Typography */}
          <section className="settings-section">
            <div className="settings-section-header">
              <Type size={16} />
              <div>
                <h3>Default Note Font</h3>
                <p>Choose your default writing typography for new notes</p>
              </div>
            </div>
            <div className="settings-font-grid">
              {fontOptions.map((opt) => (
                <button
                  type="button"
                  key={opt.id}
                  className={`settings-font-card ${defaultFont === opt.id ? "selected" : ""}`}
                  onClick={() => onDefaultFontChange(opt.id)}
                  aria-pressed={defaultFont === opt.id}
                >
                  <div className="settings-font-top">
                    <span className="font-name">{opt.label}</span>
                    {defaultFont === opt.id && <CheckCircle2 size={16} className="check-icon" />}
                  </div>
                  <div className={`settings-font-sample ${opt.className}`}>
                    Every quiet idea deserves a gentle home.
                  </div>
                </button>
              ))}
            </div>
          </section>

          {/* Section: Theme */}
          <section className="settings-section">
            <div className="settings-section-header">
              {dark ? <Moon size={16} /> : <Sun size={16} />}
              <div>
                <h3>Appearance & Theme</h3>
                <p>Switch between serene dark lake and bright daylight desktop</p>
              </div>
            </div>
            <div className="settings-theme-row">
              <div className="theme-toggle-label">
                <strong>{dark ? "Dark Lake Theme" : "Light Atmosphere"}</strong>
                <span>Pastel note sheets preserve their colored identity in both modes</span>
              </div>
              <div className="theme-switch-wrap">
                <Sun size={14} className={!dark ? "active-icon" : "dim-icon"} />
                <Switch
                  checked={dark}
                  onCheckedChange={onDarkToggle}
                  aria-label="Toggle dark mode"
                />
                <Moon size={14} className={dark ? "active-icon" : "dim-icon"} />
              </div>
            </div>
          </section>

          {/* Section: Dock Edge */}
          <section className="settings-section">
            <div className="settings-section-header">
              <ArrowRight size={16} />
              <div>
                <h3>Dock Edge Position</h3>
                <p>Snap your note decks to any screen edge for seamless access</p>
              </div>
            </div>
            <div className="settings-dock-grid">
              {[
                { id: "right" as const, label: "Right Edge", icon: ArrowRight },
                { id: "left" as const, label: "Left Edge", icon: ArrowLeft },
                { id: "top" as const, label: "Top Edge", icon: ArrowUp },
                { id: "bottom" as const, label: "Bottom Edge", icon: ArrowDown },
              ].map(({ id, label, icon: Icon }) => (
                <button
                  type="button"
                  key={id}
                  className={`settings-dock-card ${side === id ? "selected" : ""}`}
                  onClick={() => onSideChange(id)}
                  aria-pressed={side === id}
                >
                  <Icon size={16} />
                  <span>{label}</span>
                  {side === id && <span className="dock-active-indicator" />}
                </button>
              ))}
            </div>
          </section>

          {/* Section: Task Management */}
          <section className="settings-section">
            <div className="settings-section-header">
              <ListTodo size={16} />
              <div>
                <h3>Completed Checklist Items</h3>
                <p>Organize checklists inside notes automatically</p>
              </div>
            </div>
            <div className="settings-theme-row">
              <div className="theme-toggle-label">
                <strong>Move completed tasks to bottom</strong>
                <span>Keep unfinished tasks front and center while dimming completed items</span>
              </div>
              <Switch
                checked={completedTasksAtBottom}
                onCheckedChange={onCompletedTasksAtBottomChange}
                aria-label="Move completed tasks to bottom"
              />
            </div>
          </section>
        </div>

        <footer className="settings-footer">
          <span className="settings-saved-hint">
            Changes are saved automatically to local storage
          </span>
          <Button variant="outline" className="settings-done-btn" onClick={onClose}>
            Done
          </Button>
        </footer>
      </div>
    </div>
  );
}
