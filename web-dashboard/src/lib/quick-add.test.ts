import { describe, expect, it } from "vitest"

import { parseDue, parseQuickAdd } from "@/lib/quick-add"

// Wednesday 2026-10-28, local time.
const now = new Date(2026, 9, 28, 15, 30)

describe("parseDue", () => {
  it("handles keywords, offsets and ISO dates", () => {
    expect(parseDue("today", now)).toBe("2026-10-28")
    expect(parseDue("Tomorrow", now)).toBe("2026-10-29")
    expect(parseDue("+3d", now)).toBe("2026-10-31")
    expect(parseDue("+2w", now)).toBe("2026-11-11")
    expect(parseDue("2026-12-24", now)).toBe("2026-12-24")
  })

  it("picks the next weekday, never today", () => {
    expect(parseDue("fri", now)).toBe("2026-10-30")
    expect(parseDue("friday", now)).toBe("2026-10-30")
    expect(parseDue("wed", now)).toBe("2026-11-04")
    expect(parseDue("mon", now)).toBe("2026-11-02")
  })

  it("rejects things that are not dates", () => {
    expect(parseDue("2026-02-30", now)).toBeNull()
    expect(parseDue("soon", now)).toBeNull()
    expect(parseDue("monster", now)).toBeNull()
    expect(parseDue("", now)).toBeNull()
  })
})

describe("parseQuickAdd", () => {
  it("extracts priority and due date and keeps the rest as the title", () => {
    expect(parseQuickAdd("Send invoice !high due:fri", now)).toEqual({
      title: "Send invoice",
      priority: "high",
      dueDate: "2026-10-30",
    })
    expect(parseQuickAdd("  !low   Ask for cPanel access  ", now)).toEqual({
      title: "Ask for cPanel access",
      priority: "low",
      dueDate: null,
    })
  })

  it("leaves unrecognised tokens in the title", () => {
    expect(parseQuickAdd("Call back due:someday !urgent", now)).toEqual({
      title: "Call back due:someday !urgent",
      priority: "normal",
      dueDate: null,
    })
  })
})
