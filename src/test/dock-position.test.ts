import { describe, expect, it } from "vitest";
import { dockAtPoint, dockCoordinates } from "@/lib/dock-position";

describe("Dock placement", () => {
  it("snaps to every nearest edge", () => {
    expect(dockAtPoint(5, 300, 1000, 600).side).toBe("left");
    expect(dockAtPoint(995, 300, 1000, 600).side).toBe("right");
    expect(dockAtPoint(500, 5, 1000, 600).side).toBe("top");
    expect(dockAtPoint(500, 595, 1000, 600).side).toBe("bottom");
  });
  it("keeps custom corner placements inside the viewport", () => {
    expect(dockCoordinates({side: "right", position: 1}, {width: 1000, height: 600}, {width: 35, height: 400})).toEqual({left: 965, top: 200});
    expect(dockCoordinates({side: "top", position: 0}, {width: 1000, height: 600}, {width: 450, height: 35})).toEqual({left: 0, top: 0});
    expect(dockAtPoint(-50, 20, 1000, 600).position).toBeGreaterThanOrEqual(0);
  });
});