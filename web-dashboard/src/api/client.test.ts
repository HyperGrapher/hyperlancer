import { afterEach, describe, expect, it, vi } from "vitest"

import { ApiError, api, apiUrl } from "@/api/client"

function mockFetch(status: number, body: string) {
  const fn = vi
    .fn()
    .mockImplementation(async () => new Response(body, { status }))
  vi.stubGlobal("fetch", fn)
  return fn
}

afterEach(() => vi.unstubAllGlobals())

describe("apiUrl", () => {
  it("prefixes /api and skips empty query values", () => {
    expect(
      apiUrl("/sites", { q: "a b", status: "", page: 2, x: undefined })
    ).toBe("/api/sites?q=a+b&page=2")
    expect(apiUrl("health")).toBe("/api/health")
  })
})

describe("api", () => {
  it("sends the CSRF header and JSON body on mutations only", async () => {
    const fetchMock = mockFetch(200, '{"ok":true}')
    await api("/x", { method: "POST", body: { a: 1 } })
    const [, init] = fetchMock.mock.calls[0]
    expect(init.headers["X-Requested-With"]).toBe("freelance-dashboard")
    expect(init.headers["Content-Type"]).toBe("application/json")
    expect(init.body).toBe('{"a":1}')

    await api("/x")
    const [, getInit] = fetchMock.mock.calls[1]
    expect(getInit.headers["X-Requested-With"]).toBeUndefined()
    expect(getInit.body).toBeUndefined()
  })

  it("maps error envelopes to ApiError", async () => {
    mockFetch(
      422,
      '{"error":{"code":"validation_failed","message":"Bad","fields":{"url":"Invalid"}}}'
    )
    const error = await api("/x").catch((e) => e)
    expect(error).toBeInstanceOf(ApiError)
    expect(error).toMatchObject({
      status: 422,
      code: "validation_failed",
      fields: { url: "Invalid" },
    })
  })

  it("flags non-JSON success responses (SPA shell instead of the API)", async () => {
    mockFetch(200, "<!doctype html>")
    await expect(api("/health")).rejects.toMatchObject({ code: "bad_response" })
  })

  it("reports network failures", async () => {
    vi.stubGlobal("fetch", vi.fn().mockRejectedValue(new TypeError("fail")))
    await expect(api("/health")).rejects.toMatchObject({ code: "network" })
  })
})
