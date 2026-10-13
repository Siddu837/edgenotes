export type DockSide = "left" | "right" | "top" | "bottom";
export type DockPlacement = { side: DockSide; position: number };

export function dockAtPoint(x: number, y: number, width: number, height: number): DockPlacement {
  const edges: [DockSide, number][] = [["left", x], ["right", width - x], ["top", y], ["bottom", height - y]];
  edges.sort((a, b) => a[1] - b[1]);
  const side = edges[0]?.[0] || "right";
  const position = side === "left" || side === "right" ? y / height : x / width;
  return { side, position: Math.max(0, Math.min(1, position)) };
}

export function dockCoordinates(placement: DockPlacement, viewport: { width: number; height: number }, size: { width: number; height: number }) {
  const availableX = Math.max(0, viewport.width - size.width);
  const availableY = Math.max(0, viewport.height - size.height);
  return {
    left: placement.side === "left" ? 0 : placement.side === "right" ? availableX : availableX * placement.position,
    top: placement.side === "top" ? 0 : placement.side === "bottom" ? availableY : availableY * placement.position,
  };
}