import type { Priority } from "@/api/types"

export interface QuickAdd {
  title: string
  priority: Priority
  dueDate: string | null
}

const FULL_WEEKDAYS = [
  "sunday",
  "monday",
  "tuesday",
  "wednesday",
  "thursday",
  "friday",
  "saturday",
]

const pad = (n: number) => String(n).padStart(2, "0")
const iso = (date: Date) =>
  `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`

/** Resolves `today`, `tomorrow`, `fri`, `+3d`, `+2w` or `2026-11-05` against `now`; null when it is not a date. */
export function parseDue(token: string, now: Date = new Date()): string | null {
  const value = token.toLowerCase()
  const base = new Date(now.getFullYear(), now.getMonth(), now.getDate())
  const shift = (days: number) => {
    const next = new Date(base)
    next.setDate(next.getDate() + days)
    return iso(next)
  }
  if (value === "today") return iso(base)
  if (value === "tomorrow" || value === "tmr") return shift(1)
  const relative = /^\+(\d{1,3})([dw])$/.exec(value)
  if (relative)
    return shift(Number(relative[1]) * (relative[2] === "w" ? 7 : 1))
  const weekday = FULL_WEEKDAYS.findIndex(
    (name) => value === name || value === name.slice(0, 3)
  )
  if (weekday >= 0) {
    // The next such day, never today: "fri" on a Friday means next Friday.
    const ahead = ((weekday - base.getDay() + 6) % 7) + 1
    return shift(ahead)
  }
  const date = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value)
  if (date) {
    const parsed = new Date(
      Number(date[1]),
      Number(date[2]) - 1,
      Number(date[3])
    )
    return iso(parsed) === value ? value : null
  }
  return null
}

/** "Send invoice !high due:fri" becomes { title: "Send invoice", priority: "high", dueDate: ... }. */
export function parseQuickAdd(input: string, now: Date = new Date()): QuickAdd {
  let priority: Priority = "normal"
  let dueDate: string | null = null
  const words: string[] = []
  for (const word of input.trim().split(/\s+/)) {
    const flag = /^!(low|normal|high)$/i.exec(word)
    if (flag) {
      priority = flag[1].toLowerCase() as Priority
      continue
    }
    const due = /^due:(\S+)$/i.exec(word)
    if (due) {
      const parsed = parseDue(due[1], now)
      if (parsed) {
        dueDate = parsed
        continue
      }
    }
    words.push(word)
  }
  return { title: words.join(" "), priority, dueDate }
}
