import { describe, expect, it } from "vitest"

import { continueList, indent } from "@/lib/markdown-edit"

describe("continueList", () => {
  it("continues bullets, numbers and tasks", () => {
    expect(continueList("- one", 5)).toEqual({ value: "- one\n- ", caret: 8 })
    expect(continueList("2. two", 6)).toEqual({
      value: "2. two\n3. ",
      caret: 10,
    })
    expect(continueList("- [x] done", 10)).toEqual({
      value: "- [x] done\n- [ ] ",
      caret: 17,
    })
    expect(continueList("  * nested", 10)).toEqual({
      value: "  * nested\n  * ",
      caret: 15,
    })
  })

  it("ends the list on an empty item and ignores other lines", () => {
    expect(continueList("a\n- ", 4)).toEqual({ value: "a\n", caret: 2 })
    expect(continueList("plain text", 10)).toBeNull()
    expect(continueList("- one", 1)).toBeNull() // caret inside the marker
  })

  it("splits an item when the caret is in the middle", () => {
    expect(continueList("- ab", 3)).toEqual({ value: "- a\n- b", caret: 6 })
  })
})

describe("indent", () => {
  it("replaces the selection with two spaces", () => {
    expect(indent("abcd", 1, 3)).toEqual({ value: "a  d", caret: 3 })
  })
})
