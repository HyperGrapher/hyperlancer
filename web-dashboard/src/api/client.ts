// Single place that builds API URLs and sends requests (docs/DECISIONS.md D9, D12).
export class ApiError extends Error {
  status: number
  code: string
  fields: Record<string, string>
  logRef?: string
  /** The rest of the error object, for example `current` on a 409 from a note save. */
  details: Record<string, unknown>

  constructor(
    status: number,
    code: string,
    message: string,
    fields: Record<string, string> = {},
    logRef?: string,
    details: Record<string, unknown> = {}
  ) {
    super(message)
    this.name = "ApiError"
    this.status = status
    this.code = code
    this.fields = fields
    this.logRef = logRef
    this.details = details
  }
}

export const CSRF_HEADER = { "X-Requested-With": "freelance-dashboard" }

export function apiUrl(path: string, query?: Record<string, unknown>): string {
  const url = `/api${path.startsWith("/") ? path : `/${path}`}`
  if (!query) return url
  const params = new URLSearchParams()
  for (const [key, value] of Object.entries(query)) {
    if (value !== undefined && value !== null && value !== "") {
      params.set(key, String(value))
    }
  }
  const qs = params.toString()
  return qs ? `${url}?${qs}` : url
}

export async function api<T>(
  path: string,
  options: {
    method?: "GET" | "POST" | "PATCH" | "PUT" | "DELETE"
    body?: unknown
    query?: Record<string, unknown>
    signal?: AbortSignal
  } = {}
): Promise<T> {
  const method = options.method ?? "GET"
  const headers: Record<string, string> = { Accept: "application/json" }
  if (method !== "GET") {
    Object.assign(headers, CSRF_HEADER, { "Content-Type": "application/json" })
  }
  let response: Response
  try {
    response = await fetch(apiUrl(path, options.query), {
      method,
      headers,
      body:
        method === "GET"
          ? undefined
          : JSON.stringify(options.body === undefined ? {} : options.body),
      signal: options.signal,
    })
  } catch {
    throw new ApiError(
      0,
      "network",
      "Could not reach the API. Is the PHP server running?"
    )
  }
  if (response.status === 204) return undefined as T
  const text = await response.text()
  let data: unknown = null
  try {
    data = text ? JSON.parse(text) : null
  } catch {
    // Not JSON, for example the SPA shell served by a server without /api routing.
  }
  if (!response.ok) {
    const error = (data as { error?: Record<string, unknown> } | null)?.error
    throw new ApiError(
      response.status,
      typeof error?.code === "string" ? error.code : "http_error",
      typeof error?.message === "string"
        ? error.message
        : `Request failed (${response.status}).`,
      (error?.fields as Record<string, string>) ?? {},
      typeof error?.logRef === "string" ? error.logRef : undefined,
      error ?? {}
    )
  }
  if (data === null) {
    throw new ApiError(
      502,
      "bad_response",
      "The API returned something that is not JSON. Check that /api/* reaches the PHP backend."
    )
  }
  return data as T
}
