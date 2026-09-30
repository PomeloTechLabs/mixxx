export interface SafeRect {
  left: number;
  top: number;
  width: number;
  height: number;
}

export interface SafeInsets {
  left: number;
  top: number;
  right: number;
  bottom: number;
}

export function calculateSafeInsets(rect: SafeRect, displayWidth: number, displayHeight: number,
    density: number, radius: number, obstacles: SafeRect[]): SafeInsets {
  const result: SafeInsets = { left: 0, top: 0, right: 0, bottom: 0 };
  const phone: boolean = Math.min(displayWidth, displayHeight) / Math.max(1, density) < 600;
  const corner: number = radius > 0
    ? Math.ceil(radius * (1 - 1 / Math.sqrt(2)) + 4 * density)
    : Math.ceil((phone ? 18 : 4) * density);
  const tolerance: number = Math.max(2, density);
  if (rect.left <= tolerance) { result.left = Math.max(0, corner - rect.left); }
  if (rect.top <= tolerance) { result.top = Math.max(0, corner - rect.top); }
  if (rect.left + rect.width >= displayWidth - tolerance) {
    result.right = Math.max(0, rect.left + rect.width - displayWidth + corner);
  }
  if (rect.top + rect.height >= displayHeight - tolerance) {
    result.bottom = Math.max(0, rect.top + rect.height - displayHeight + corner);
  }
  obstacles.forEach((obstacle: SafeRect): void => {
    const left = Math.max(rect.left, obstacle.left);
    const top = Math.max(rect.top, obstacle.top);
    const right = Math.min(rect.left + rect.width, obstacle.left + obstacle.width);
    const bottom = Math.min(rect.top + rect.height, obstacle.top + obstacle.height);
    if (right <= left || bottom <= top) { return; }
    const distances: number[] = [right - rect.left, bottom - rect.top,
      rect.left + rect.width - left, rect.top + rect.height - top];
    const edge: number = distances.indexOf(Math.min(...distances));
    const margin: number = Math.ceil(distances[edge] + 4 * density);
    if (edge === 0) { result.left = Math.max(result.left, margin); }
    if (edge === 1) { result.top = Math.max(result.top, margin); }
    if (edge === 2) { result.right = Math.max(result.right, margin); }
    if (edge === 3) { result.bottom = Math.max(result.bottom, margin); }
  });
  result.left = Math.min(Math.ceil(result.left), Math.floor(rect.width / 3));
  result.right = Math.min(Math.ceil(result.right), Math.floor(rect.width / 3));
  result.top = Math.min(Math.ceil(result.top), Math.floor(rect.height / 3));
  result.bottom = Math.min(Math.ceil(result.bottom), Math.floor(rect.height / 3));
  return result;
}
