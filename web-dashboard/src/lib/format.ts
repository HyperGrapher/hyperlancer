import {
  differenceInCalendarDays,
  format,
  formatDistanceToNowStrict,
  parseISO,
} from "date-fns"

export function relativeTime(iso: string | null): string {
  if (!iso) return "never"
  return formatDistanceToNowStrict(parseISO(iso), { addSuffix: true })
}

export function shortDate(iso: string | null): string {
  if (!iso) return "—"
  return format(parseISO(iso), "d MMM yyyy")
}

export function daysUntil(isoDate: string | null): number | null {
  if (!isoDate) return null
  return differenceInCalendarDays(parseISO(isoDate), new Date())
}

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  const units = ["KB", "MB", "GB", "TB"]
  let value = bytes / 1024
  let unit = 0
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024
    unit++
  }
  return `${value.toFixed(value < 10 ? 1 : 0)} ${units[unit]}`
}

export function dueLabel(isoDate: string | null): string {
  const days = daysUntil(isoDate)
  if (days === null) return "No date"
  if (days < 0) return `${-days}d overdue`
  if (days === 0) return "Today"
  if (days === 1) return "Tomorrow"
  if (days < 7) return format(parseISO(isoDate!), "EEE d MMM")
  return format(parseISO(isoDate!), "d MMM")
}
