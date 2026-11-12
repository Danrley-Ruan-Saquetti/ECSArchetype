export interface Drawer {
  clear(): void;
  circle(x: number, y: number, radius: number, color: string): void;
  rect(x: number, y: number, width: number, height: number, color: string): void;
  text(x: number, y: number, content: string, color: string): void;
}
