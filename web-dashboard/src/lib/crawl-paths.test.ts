import { describe, expect, it } from "vitest"

import { resolveRelative, withLocalImages } from "./crawl-paths"

describe("crawl paths", () => {
  it("resolves relative to the page folder", () => {
    expect(resolveRelative("pages/about.md", "../images/a.png")).toBe(
      "images/a.png"
    )
    expect(resolveRelative("pages/en/about.md", "../../images/a.png")).toBe(
      "images/a.png"
    )
  })

  it("rewrites relative links and leaves absolute ones", () => {
    const out = withLocalImages(
      "![a](../images/a%20b.png) [x](https://e.com/) [y](about.md)",
      "pages/index.md",
      (p) => `/raw?path=${p}`
    )
    expect(out).toBe(
      "![a](/raw?path=images/a b.png) [x](https://e.com/) [y](about.md)"
    )
  })
})
