import { describe, expect, it } from "vitest"

import {
  joinList,
  parseStatuses,
  sitesSearchSchema,
  splitList,
} from "@/lib/site-filters"

describe("sitesSearchSchema", () => {
  it("round-trips URL search params", () => {
    const parsed = sitesSearchSchema.parse({
      status: "design,waiting",
      tag: "seo",
      view: "cards",
      page: "2",
      q: "fora",
    })
    expect(parsed).toMatchObject({
      status: "design,waiting",
      tag: "seo",
      view: "cards",
      page: 2,
      q: "fora",
    })
    expect(splitList(parsed.status)).toEqual(["design", "waiting"])
    expect(joinList(["a", "b"])).toBe("a,b")
    expect(joinList([])).toBeUndefined()
  })

  it("drops invalid values instead of throwing", () => {
    const parsed = sitesSearchSchema.parse({
      view: "grid",
      page: "-3",
      archived: 2,
    })
    expect(parsed.view).toBeUndefined()
    expect(parsed.page).toBeUndefined()
    expect(parsed.archived).toBeUndefined()
  })

  it("keeps numeric-looking values as strings (the router parses ?q=2024 as a number)", () => {
    expect(
      sitesSearchSchema.parse({ q: 2024, tag: 7, archived: 1 })
    ).toMatchObject({ q: "2024", tag: "7", archived: "1" })
  })

  it("keeps only known statuses", () => {
    expect(parseStatuses("design,bogus,done")).toEqual(["design", "done"])
  })
})
