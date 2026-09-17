export function SaveStateBadge({ state, label }: { state: "Saved" | "Unsaved" | "Empty slot"; label: string }) {
  return <span className={`badge badge-sm ${state === "Unsaved" ? "badge-warning" : "badge-ghost"}`} role="status" aria-label={label}>{state}</span>;
}
