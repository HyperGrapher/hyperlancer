import {
  ActivityIcon,
  ChevronRightIcon,
  ClockIcon,
  CpuIcon,
  DownloadIcon,
  LockOpenIcon,
  LoaderCircleIcon,
  ExternalLinkIcon,
  GaugeIcon,
  GlobeIcon,
  HistoryIcon,
  LayersIcon,
  LockIcon,
  MailIcon,
  NetworkIcon,
  SearchIcon,
  ServerIcon,
  ShieldCheckIcon,
  TriangleAlertIcon,
} from "lucide-react"
import { useState, type ReactNode } from "react"
import { cn } from "cn"

import { ConfidenceDot } from "@/components/common/badges"
import {
  CopyButton,
  EmptyState,
  KeyValue,
  StatCard,
} from "@/components/common/misc"
import { Button } from "@/components/ui/button"
import {
  Collapsible,
  CollapsibleContent,
  CollapsibleTrigger,
} from "@/components/ui/collapsible"
import { Input } from "@/components/ui/input"
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from "@/components/ui/popover"
import { formatBytes, relativeTime, shortDate } from "@/lib/format"
import { getRouteApi } from "@tanstack/react-router"
import { toast } from "sonner"

import { useFetchSite, useSite, useSnapshot } from "@/api/hooks/sites"
import type { ProfileSummary, SiteDetail, Technology } from "@/api/types"

const routeApi = getRouteApi("/sites/$siteId")

function Section({
  title,
  icon,
  summary,
  children,
  defaultOpen = true,
}: {
  title: string
  icon: ReactNode
  summary?: ReactNode
  children: ReactNode
  defaultOpen?: boolean
}) {
  return (
    <Collapsible
      defaultOpen={defaultOpen}
      className="rounded-xl border border-border bg-card/60"
    >
      <CollapsibleTrigger className="group flex w-full items-center gap-2 px-4 py-3 text-left">
        <ChevronRightIcon className="size-4 text-muted-foreground transition-transform group-data-panel-open:rotate-90" />
        <span className="text-muted-foreground [&_svg]:size-4">{icon}</span>
        <span className="font-medium">{title}</span>
        {summary && (
          <span className="ml-auto truncate pl-3 text-xs text-muted-foreground">
            {summary}
          </span>
        )}
      </CollapsibleTrigger>
      <CollapsibleContent>
        <div className="border-t border-border px-4 py-3">{children}</div>
      </CollapsibleContent>
    </Collapsible>
  )
}

function Check({ ok, label }: { ok: boolean; label: string }) {
  return (
    <span
      className={cn(
        "inline-flex h-6 items-center gap-1 rounded-md border px-2 text-xs",
        ok
          ? "border-emerald-500/30 bg-emerald-500/10 text-emerald-700 dark:text-emerald-300"
          : "border-rose-500/30 bg-rose-500/10 text-rose-700 dark:text-rose-300"
      )}
    >
      {ok ? "✓" : "✗"} {label}
    </span>
  )
}

function LengthGauge({
  length,
  min,
  max,
}: {
  length: number
  min: number
  max: number
}) {
  const ok = length >= min && length <= max
  return (
    <div className="flex items-center gap-2 text-xs">
      <div className="h-1.5 w-24 overflow-hidden rounded-full bg-muted">
        <div
          className={cn(
            "h-full rounded-full",
            ok ? "bg-emerald-400" : "bg-amber-400"
          )}
          style={{ width: `${Math.min(100, (length / max) * 100)}%` }}
        />
      </div>
      <span
        className={
          ok ? "text-muted-foreground" : "text-amber-600 dark:text-amber-300"
        }
      >
        {length} / {min}–{max}
      </span>
    </div>
  )
}

function TechBadge({ tech }: { tech: Technology }) {
  return (
    <Popover>
      <PopoverTrigger className="inline-flex h-7 items-center gap-1.5 rounded-lg border border-border bg-muted/30 px-2.5 text-sm hover:bg-muted">
        <ConfidenceDot confidence={tech.confidence} />
        {tech.name}
        {tech.version && (
          <span className="text-xs text-muted-foreground">{tech.version}</span>
        )}
      </PopoverTrigger>
      <PopoverContent className="w-72 text-xs">
        <p className="font-medium">
          {tech.name} · {tech.confidence} confidence
        </p>
        <ul className="mt-1 list-disc pl-4 text-muted-foreground">
          {tech.evidence.map((item, index) => (
            <li key={index}>{item}</li>
          ))}
        </ul>
      </PopoverContent>
    </Popover>
  )
}

const dash = "—"
const ms = (value: number | null) => (value === null ? dash : `${value} ms`)

function JsonView({ value, name }: { value: unknown; name: string }) {
  const [filter, setFilter] = useState("")
  const text = JSON.stringify(value, null, 2)
  // Text-only rendering keeps hostile page content inert; filtering keeps matching lines.
  const lines = filter
    ? text
        .split("\n")
        .filter((line) => line.toLowerCase().includes(filter.toLowerCase()))
    : null

  function download() {
    const url = URL.createObjectURL(
      new Blob([text], { type: "application/json" })
    )
    const link = document.createElement("a")
    link.href = url
    link.download = name
    link.click()
    URL.revokeObjectURL(url)
  }

  return (
    <div className="flex flex-col gap-2">
      <div className="flex items-center gap-2">
        <Input
          value={filter}
          onChange={(event) => setFilter(event.target.value)}
          placeholder="Filter lines…"
          className="h-8 max-w-xs"
        />
        <CopyButton value={text} />
        <Button variant="outline" size="sm" onClick={download}>
          <DownloadIcon /> Download .json
        </Button>
      </div>
      <pre className="max-h-[600px] overflow-auto rounded-xl border border-border bg-muted/30 p-4 font-mono text-xs leading-relaxed">
        {lines ? lines.join("\n") || "No matching lines." : text}
      </pre>
    </div>
  )
}

function OgPreview({ seo }: { seo: ProfileSummary["seo"] }) {
  if (!seo.ogTitle && !seo.ogImage) return null
  return (
    <div className="flex max-w-lg overflow-hidden rounded-lg border border-border">
      {seo.ogImage && (
        <img
          src={seo.ogImage}
          alt=""
          className="h-24 w-36 shrink-0 bg-muted object-cover"
          referrerPolicy="no-referrer"
          loading="lazy"
        />
      )}
      <div className="min-w-0 p-2 text-xs">
        <p className="truncate font-medium">{seo.ogTitle ?? seo.title}</p>
        <p className="line-clamp-2 text-muted-foreground">
          {seo.og?.description ?? seo.description}
        </p>
        <p className="mt-1 truncate text-muted-foreground">
          {seo.og?.site_name ?? seo.og?.url}
        </p>
      </div>
    </div>
  )
}

function ProfileSections({ profile }: { profile: ProfileSummary }) {
  const [urlFilter, setUrlFilter] = useState("")
  const techByCategory = profile.tech.reduce<Record<string, Technology[]>>(
    (groups, tech) => {
      ;(groups[tech.category] ??= []).push(tech)
      return groups
    },
    {}
  )
  const urls = profile.sitemaps.urls.filter((item) =>
    item.loc.includes(urlFilter)
  )

  return (
    <div className="flex flex-col gap-3">
      <Section
        title="Tech stack"
        icon={<CpuIcon />}
        summary={`${profile.tech.length} detections`}
      >
        <div className="flex flex-col gap-3">
          {Object.entries(techByCategory).map(([category, items]) => (
            <div key={category}>
              <p className="mb-1.5 text-xs text-muted-foreground">{category}</p>
              <div className="flex flex-wrap gap-1.5">
                {items.map((tech) => (
                  <TechBadge
                    key={`${tech.name}-${tech.version ?? ""}`}
                    tech={tech}
                  />
                ))}
              </div>
            </div>
          ))}
          {(profile.cms.theme || profile.cms.plugins.length > 0) && (
            <p className="text-sm">
              <span className="text-muted-foreground">WordPress:</span>{" "}
              {profile.cms.theme && (
                <>
                  theme <b>{profile.cms.theme}</b>
                </>
              )}
              {profile.cms.plugins.length > 0 && (
                <> · plugins {profile.cms.plugins.join(", ")}</>
              )}
            </p>
          )}
          <p className="text-xs text-muted-foreground">
            Click a badge to see the evidence. Detections are heuristic.
          </p>
        </div>
      </Section>

      <Section
        title="Design age"
        icon={<HistoryIcon />}
        summary={
          profile.design.estimatedFrom
            ? `oldest dated asset ${profile.design.estimatedFrom}`
            : "no dated clues found"
        }
        defaultOpen={Boolean(profile.design.estimatedFrom)}
      >
        {profile.design.clues.length === 0 ? (
          <p className="text-sm text-muted-foreground">
            No dated files, upload paths or copyright years were found on the
            home page.
          </p>
        ) : (
          <>
            {profile.design.estimatedFrom && (
              <p className="mb-3 text-sm">
                The dated files on this page go back to{" "}
                <b>{profile.design.estimatedFrom}</b>
                {profile.design.latestAsset &&
                  profile.design.latestAsset !==
                    profile.design.estimatedFrom && (
                    <>
                      {" "}
                      and up to <b>{profile.design.latestAsset}</b>
                    </>
                  )}
                . Confidence: {profile.design.confidence ?? "low"}.
              </p>
            )}
            <ul className="divide-y divide-border rounded-lg border border-border text-sm">
              {profile.design.clues.map((clue, index) => (
                <li
                  key={index}
                  className="flex flex-wrap items-baseline gap-x-3 gap-y-0.5 px-3 py-1.5"
                >
                  <span className="w-24 shrink-0 font-mono text-xs">
                    {clue.date}
                  </span>
                  <span className="min-w-0 flex-1">
                    {clue.label}
                    {clue.detail && (
                      <span className="block truncate text-xs text-muted-foreground">
                        {clue.detail}
                      </span>
                    )}
                  </span>
                  <span
                    className={cn(
                      "rounded border px-1.5 text-xs",
                      clue.strength === "strong"
                        ? "border-emerald-500/30 text-emerald-700 dark:text-emerald-300"
                        : clue.strength === "context"
                          ? "border-border text-muted-foreground"
                          : "border-amber-500/30 text-amber-700 dark:text-amber-300"
                    )}
                  >
                    {clue.strength === "context" ? "context" : clue.strength}
                  </span>
                </li>
              ))}
            </ul>
            <p className="mt-2 text-xs text-muted-foreground">
              These are hints, not proof: a redesign often reuses old uploads,
              and file dates change when a site is redeployed. “Context” rows
              show how old the domain and the site are, not the design.
            </p>
          </>
        )}
      </Section>

      <Section
        title="Hosting, DNS & email"
        icon={<NetworkIcon />}
        summary={`${profile.hosting.ips.join(", ") || "no IP"} · ${profile.hosting.edge ?? profile.hosting.org ?? "unknown host"}`}
      >
        <div className="grid gap-x-8 md:grid-cols-2">
          <dl>
            {profile.hosting.ips.map((ip) => (
              <KeyValue key={ip} label="A" value={ip} copy={ip} mono />
            ))}
            {profile.hosting.ipv6.map((ip) => (
              <KeyValue key={ip} label="AAAA" value={ip} copy={ip} mono />
            ))}
            <KeyValue
              label="Network"
              value={
                profile.hosting.org
                  ? `${profile.hosting.org}${profile.hosting.asn ? ` (${profile.hosting.asn})` : ""}`
                  : profile.hosting.asn
              }
            />
            <KeyValue label="Edge / CDN" value={profile.hosting.edge} />
            <KeyValue
              label="Reverse DNS"
              value={profile.hosting.reverseDns}
              mono
            />
            <KeyValue label="Server" value={profile.hosting.server} />
            <KeyValue
              label="Language"
              value={
                profile.hosting.language
                  ? `${profile.hosting.language}${profile.hosting.languageVersion ? ` ${profile.hosting.languageVersion}` : ""}`
                  : null
              }
            />
          </dl>
          <dl>
            {profile.dns.ns.map((ns) => (
              <KeyValue
                key={ns}
                label={
                  profile.dns.nsSource === "registry" ? "NS (registry)" : "NS"
                }
                value={ns}
                copy={ns}
                mono
              />
            ))}
            {profile.dns.mx.map((mx) => (
              <KeyValue
                key={mx.host}
                label={`MX ${mx.pref}`}
                value={mx.host}
                copy={mx.host}
                mono
              />
            ))}
            <KeyValue
              label="Email provider"
              value={profile.dns.emailProvider}
            />
          </dl>
        </div>
        <div className="mt-3 flex flex-wrap gap-1.5">
          <Check ok={Boolean(profile.dns.spf)} label="SPF" />
          <Check ok={Boolean(profile.dns.dmarc)} label="DMARC" />
          <Check ok={profile.dns.dnssec} label="DNSSEC" />
          <Check ok={profile.dns.caa.length > 0} label="CAA" />
        </div>
        {profile.dns.spf && (
          <div className="mt-3 flex items-center gap-1 rounded-lg bg-muted/40 px-2 py-1.5 font-mono text-xs break-all">
            {profile.dns.spf}
            <CopyButton value={profile.dns.spf} />
          </div>
        )}
      </Section>

      <Section
        title="SSL / TLS & domain"
        icon={<LockIcon />}
        summary={`SSL ${profile.tls.daysLeft !== null ? Math.round(profile.tls.daysLeft) : "?"} days · domain ${shortDate(profile.domain.expires)}`}
      >
        <div className="grid gap-x-8 md:grid-cols-2">
          <dl>
            <KeyValue
              label="Verified"
              value={profile.tls.verified ? "Yes" : "No"}
            />
            <KeyValue label="Issuer" value={profile.tls.issuer} />
            <KeyValue
              label="Valid from"
              value={shortDate(profile.tls.validFrom)}
            />
            <KeyValue
              label="Valid until"
              value={shortDate(profile.tls.validUntil)}
            />
            <KeyValue label="SAN names" value={profile.tls.sanCount} />
            <KeyValue
              label="Fingerprint"
              value={
                profile.tls.fingerprint ? (
                  <span className="truncate">
                    {profile.tls.fingerprint.slice(0, 23)}…
                  </span>
                ) : null
              }
              copy={profile.tls.fingerprint ?? undefined}
              mono
            />
            {profile.tls.error && (
              <KeyValue label="Problem" value={profile.tls.error} />
            )}
          </dl>
          <dl>
            <KeyValue label="Registrant" value={profile.domain.registrant} />
            <KeyValue label="Registrar" value={profile.domain.registrar} />
            <KeyValue
              label="Transfer"
              value={
                profile.domain.transferLocked === null ? null : profile.domain
                    .transferLocked ? (
                  <span className="inline-flex items-center gap-1 text-amber-600 dark:text-amber-300">
                    <LockIcon className="size-3.5" /> Locked
                  </span>
                ) : (
                  <span className="inline-flex items-center gap-1 text-emerald-600 dark:text-emerald-300">
                    <LockOpenIcon className="size-3.5" /> Not locked
                  </span>
                )
              }
            />
            <KeyValue
              label="Created"
              value={shortDate(profile.domain.created)}
            />
            <KeyValue
              label="Expires"
              value={shortDate(profile.domain.expires)}
            />
            <KeyValue
              label="Age"
              value={
                profile.domain.ageYears === null
                  ? null
                  : `${profile.domain.ageYears} years`
              }
            />
            <KeyValue
              label="Status"
              value={profile.domain.statuses.join(", ")}
            />
            <KeyValue label="Lookup" value={profile.domain.method} />
            {profile.dns.nsSource === "registry" && (
              <KeyValue
                label="Nameservers"
                value="from the registry (DNS NS lookup unavailable)"
              />
            )}
          </dl>
        </div>
      </Section>

      <Section
        title="SEO & meta"
        icon={<SearchIcon />}
        summary={`H1: ${profile.seo.h1Count} · lang ${profile.seo.lang ?? dash}`}
      >
        <div className="flex flex-col gap-3">
          <div>
            <div className="flex items-center justify-between gap-2">
              <span className="text-xs text-muted-foreground">Title</span>
              <LengthGauge
                length={
                  profile.seo.titleLength || profile.seo.title?.length || 0
                }
                min={30}
                max={60}
              />
            </div>
            <p className="mt-0.5">{profile.seo.title}</p>
          </div>
          <div>
            <div className="flex items-center justify-between gap-2">
              <span className="text-xs text-muted-foreground">Description</span>
              <LengthGauge
                length={
                  profile.seo.descriptionLength ||
                  profile.seo.description?.length ||
                  0
                }
                min={70}
                max={160}
              />
            </div>
            <p className="mt-0.5 text-muted-foreground">
              {profile.seo.description}
            </p>
          </div>
          <div className="flex flex-wrap gap-1.5">
            <Check
              ok={profile.seo.h1Count === 1}
              label={`H1 count: ${profile.seo.h1Count}`}
            />
            <Check ok={Boolean(profile.seo.canonical)} label="Canonical" />
            <Check ok={Boolean(profile.seo.ogTitle)} label="Open Graph" />
            <Check
              ok={profile.seo.structuredData.length > 0}
              label="Structured data"
            />
            <Check
              ok={profile.seo.imagesMissingAlt === 0}
              label={`${profile.seo.imagesMissingAlt}/${profile.seo.imagesTotal} images without alt`}
            />
            {profile.seo.noindex && <Check ok={false} label="noindex" />}
            {profile.seo.consentTool && (
              <Check ok label={`Consent: ${profile.seo.consentTool}`} />
            )}
          </div>
          <OgPreview seo={profile.seo} />
          {profile.seo.hreflang.length > 0 && (
            <p className="text-xs text-muted-foreground">
              hreflang:{" "}
              {profile.seo.hreflang.map((item) => item.lang).join(", ")}
            </p>
          )}
          {profile.seo.links && (
            <p className="text-xs text-muted-foreground">
              Links on the page: {profile.seo.links.internal} internal,{" "}
              {profile.seo.links.external} external
            </p>
          )}
          <div>
            <p className="mb-1 text-xs text-muted-foreground">
              Heading outline
            </p>
            <ul className="text-sm">
              {profile.seo.headings.length === 0 && (
                <li className="text-muted-foreground">No headings found.</li>
              )}
              {profile.seo.headings.map((heading, index) => (
                <li
                  key={index}
                  style={{ paddingLeft: (heading.level - 1) * 14 }}
                >
                  <span className="mr-1.5 font-mono text-xs text-muted-foreground">
                    H{heading.level}
                  </span>
                  {heading.text}
                </li>
              ))}
            </ul>
          </div>
        </div>
      </Section>

      <Section
        title="Performance"
        icon={<GaugeIcon />}
        summary={`TTFB ${ms(profile.http.ttfbMs)} · ${profile.http.htmlBytes === null ? dash : formatBytes(profile.http.htmlBytes)}`}
      >
        <div className="grid grid-cols-2 gap-2 md:grid-cols-4">
          <StatCard label="TTFB" value={ms(profile.http.ttfbMs)} />
          <StatCard label="Total" value={ms(profile.http.totalMs)} />
          <StatCard
            label="HTML size"
            value={
              profile.http.htmlBytes === null
                ? dash
                : formatBytes(profile.http.htmlBytes)
            }
          />
          <StatCard
            label="Compression"
            value={profile.http.compression ?? "none"}
          />
        </div>
        <div className="mt-3 flex flex-wrap gap-1.5">
          {profile.http.httpsRedirect !== null && (
            <Check
              ok={profile.http.httpsRedirect}
              label="HTTP → HTTPS redirect"
            />
          )}
          {profile.http.wwwCanonical && profile.http.wwwRedirect !== null && (
            <Check
              ok={profile.http.wwwRedirect}
              label={`${profile.http.wwwCanonical === "www" ? "apex → www" : "www → apex"} redirect`}
            />
          )}
          <Check
            ok={profile.url.redirects.length <= 1}
            label={`${profile.url.redirects.length} redirects`}
          />
        </div>
      </Section>

      <Section
        title="Pages & sitemap"
        icon={<LayersIcon />}
        summary={`${profile.sitemaps.totalUrls} URLs`}
      >
        <div className="mb-2 flex flex-wrap items-center gap-2 text-sm">
          <span className="text-muted-foreground">
            robots.txt:{" "}
            {profile.sitemaps.robotsStatus === null
              ? "not fetched"
              : `HTTP ${profile.sitemaps.robotsStatus}`}
            {profile.sitemaps.disallowRules > 0 &&
              ` · ${profile.sitemaps.disallowRules} disallow rules`}
          </span>
          {profile.sitemaps.files.map((file) => (
            <a
              key={file.url}
              href={file.url}
              target="_blank"
              rel="noopener noreferrer"
              className="text-xs underline underline-offset-2"
            >
              {file.url.replace(/^https?:\/\//, "")}
              {file.urls !== null && ` (${file.urls} URLs)`}
              {file.kind === "sitemapindex" && " (index)"}
            </a>
          ))}
          <Input
            value={urlFilter}
            onChange={(event) => setUrlFilter(event.target.value)}
            placeholder="Filter URLs…"
            className="ml-auto h-7 w-48"
          />
        </div>
        {profile.sitemaps.files.some((file) => file.error || file.warning) && (
          <ul className="mb-2 text-xs text-amber-700 dark:text-amber-300">
            {profile.sitemaps.files.map((file) =>
              file.error || file.warning ? (
                <li key={file.url}>
                  {file.url.replace(/^https?:\/\//, "")}:{" "}
                  {file.error ?? file.warning}
                </li>
              ) : null
            )}
          </ul>
        )}
        <div className="max-h-96 divide-y divide-border overflow-auto rounded-lg border border-border">
          {urls.slice(0, 200).map((item) => (
            <div
              key={item.loc}
              className="flex items-center justify-between gap-3 px-3 py-1.5 text-sm"
            >
              <a
                href={item.loc}
                target="_blank"
                rel="noopener noreferrer"
                className="truncate hover:underline"
              >
                {item.loc.replace(/^https?:\/\/[^/]+/, "") || "/"}
              </a>
              <span className="shrink-0 text-xs text-muted-foreground">
                {item.lastmod ?? ""}
              </span>
            </div>
          ))}
        </div>
        <p className="mt-2 text-xs text-muted-foreground">
          Showing {Math.min(urls.length, 200)} of {profile.sitemaps.totalUrls}{" "}
          URLs
          {profile.sitemaps.urls.length < profile.sitemaps.totalUrls &&
            ` (the profile keeps the first ${profile.sitemaps.urls.length})`}
          .
        </p>
      </Section>

      <Section
        title="Contact & social"
        icon={<MailIcon />}
        summary={profile.contact.emails[0] ?? profile.contact.phones[0]}
      >
        <dl>
          {profile.contact.emails.map((email) => (
            <KeyValue key={email} label="Email" value={email} copy={email} />
          ))}
          {profile.contact.phones.map((phone) => (
            <KeyValue key={phone} label="Phone" value={phone} copy={phone} />
          ))}
          {profile.contact.whatsapp.map((link) => (
            <KeyValue key={link} label="WhatsApp" value={link} copy={link} />
          ))}
          {profile.contact.address && (
            <KeyValue
              label="Address"
              value={profile.contact.address}
              copy={profile.contact.address}
            />
          )}
          {profile.contact.socials.map((social) => (
            <KeyValue
              key={social.url}
              label={social.network}
              value={
                <a
                  href={social.url}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="flex items-center gap-1 hover:underline"
                >
                  {social.url.replace(/^https?:\/\//, "")}{" "}
                  <ExternalLinkIcon className="size-3" />
                </a>
              }
            />
          ))}
        </dl>
      </Section>

      <Section
        title="Security headers & cookies"
        icon={<ShieldCheckIcon />}
        summary={`${profile.security.present.length}/${profile.security.present.length + profile.security.missing.length} headers`}
        defaultOpen={false}
      >
        <div className="flex flex-wrap gap-1.5">
          {profile.security.present.map((name) => (
            <Check key={name} ok label={name} />
          ))}
          {profile.security.missing.map((name) => (
            <Check key={name} ok={false} label={name} />
          ))}
        </div>
        <div className="mt-3 text-sm">
          {profile.security.cookies.map((cookie) => (
            <div
              key={cookie.name}
              className="flex flex-wrap items-center gap-2"
            >
              <span className="font-mono text-xs">{cookie.name}</span>
              <Check ok={cookie.secure} label="Secure" />
              <Check ok={cookie.httpOnly} label="HttpOnly" />
              <Check
                ok={Boolean(cookie.sameSite)}
                label={`SameSite ${cookie.sameSite ?? "not set"}`}
              />
            </div>
          ))}
        </div>
      </Section>

      <Section
        title="Web history"
        icon={<ClockIcon />}
        summary={
          profile.archive.first
            ? `first seen ${profile.archive.first}`
            : "no captures"
        }
        defaultOpen={false}
      >
        <dl>
          <KeyValue
            label="Earliest Wayback capture"
            value={
              profile.archive.first && profile.archive.firstUrl ? (
                <a
                  href={profile.archive.firstUrl}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="hover:underline"
                >
                  {profile.archive.first}
                </a>
              ) : (
                profile.archive.first
              )
            }
          />
          <KeyValue
            label="Latest Wayback capture"
            value={
              profile.archive.last && profile.archive.lastUrl ? (
                <a
                  href={profile.archive.lastUrl}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="hover:underline"
                >
                  {profile.archive.last}
                </a>
              ) : (
                profile.archive.last
              )
            }
          />
        </dl>
      </Section>

      <ul className="px-1 text-xs text-muted-foreground">
        {profile.limitations.map((item, index) => (
          <li key={index}>· {item}</li>
        ))}
      </ul>
    </div>
  )
}

function problemBanner(profile: ProfileSummary) {
  if (profile.outcome === "ok" || profile.errors.length === 0) return null
  const failed = profile.outcome === "failed"
  return (
    <div
      className={cn(
        "rounded-xl border px-4 py-3 text-sm",
        failed
          ? "border-rose-500/30 bg-rose-500/5"
          : "border-amber-500/30 bg-amber-500/5"
      )}
    >
      <div
        className={cn(
          "flex items-center gap-2 font-medium",
          failed
            ? "text-rose-700 dark:text-rose-300"
            : "text-amber-700 dark:text-amber-300"
        )}
      >
        <TriangleAlertIcon className="size-4" />
        {failed
          ? "Profile could not be fetched"
          : `Partial profile: ${profile.errors.length} problem${profile.errors.length === 1 ? "" : "s"}`}
        {profile.http.blocked &&
          ` · blocked by bot protection (${profile.http.blockedBy ?? "unknown"})`}
      </div>
      <ul className="mt-1 text-muted-foreground">
        {profile.errors.map((error, index) => (
          <li key={index}>
            <span className="font-mono text-xs">{error.section}</span>:{" "}
            {error.message}
          </li>
        ))}
      </ul>
    </div>
  )
}

function FetchButton({
  site,
  label = "Fetch profile",
}: {
  site: SiteDetail
  label?: string
}) {
  const fetchProfile = useFetchSite(site.id)
  return (
    <Button
      variant="outline"
      size="sm"
      disabled={!!site.activeJob || fetchProfile.isPending}
      onClick={() =>
        fetchProfile.mutate(undefined, {
          onError: (e) => toast.error(e.message),
        })
      }
    >
      {site.activeJob ? <LoaderCircleIcon className="animate-spin" /> : null}
      {label}
    </Button>
  )
}

export function ProfileTab() {
  const { siteId } = routeApi.useParams()
  const { data: site } = useSite(siteId)
  const snapshotId = site?.latestSnapshot?.id
  const { data: snapshot, isLoading } = useSnapshot(snapshotId)
  const [tab, setTab] = useState<"profile" | "raw">("profile")
  const raw = useSnapshot(tab === "raw" ? snapshotId : null, true)

  if (!site) return null

  if (!snapshotId) {
    if (site.activeJob) {
      return (
        <EmptyState
          icon={<LoaderCircleIcon className="animate-spin" />}
          title="Fetching the profile…"
          description="This takes 10 to 40 seconds. The page updates by itself."
        />
      )
    }
    return (
      <EmptyState
        icon={<ActivityIcon />}
        title="No profile yet"
        description="Fetch a profile to see the tech stack, hosting, DNS, SSL, SEO and contact details."
        action={<FetchButton site={site} />}
      />
    )
  }
  if (isLoading || !snapshot)
    return <div className="h-40 animate-pulse rounded-xl bg-muted/40" />

  const profile = snapshot.summary
  const sslDays =
    profile.tls.daysLeft !== null ? Math.round(profile.tls.daysLeft) : null
  const newerFailure =
    site.lastAttempt &&
    site.lastAttempt.id !== snapshot.id &&
    site.lastAttempt.outcome === "failed"
      ? site.lastAttempt
      : null

  return (
    <div className="flex flex-col gap-4">
      {newerFailure && (
        <div className="rounded-xl border border-rose-500/30 bg-rose-500/5 px-4 py-3 text-sm">
          <div className="flex items-center gap-2 font-medium text-rose-700 dark:text-rose-300">
            <TriangleAlertIcon className="size-4" /> The last fetch failed{" "}
            {relativeTime(newerFailure.fetchedAt)}
          </div>
          <p className="mt-1 text-muted-foreground">
            {newerFailure.errorSummary ?? "No details."} The profile below is
            from {relativeTime(snapshot.fetchedAt)}.
          </p>
          <div className="mt-2">
            <FetchButton site={site} label="Try again" />
          </div>
        </div>
      )}
      {problemBanner(profile)}

      <div className="grid grid-cols-2 gap-2 sm:grid-cols-3 lg:grid-cols-4 2xl:grid-cols-9">
        <StatCard
          icon={<GlobeIcon />}
          label="HTTP"
          value={profile.http.status ?? dash}
          hint={`${profile.url.redirects.length} redirect${profile.url.redirects.length === 1 ? "" : "s"}`}
          tone={
            profile.http.status && profile.http.status < 400
              ? "good"
              : profile.http.status
                ? "bad"
                : "default"
          }
        />
        <StatCard
          icon={<CpuIcon />}
          label="CMS"
          value={profile.cms.name ?? "Unknown"}
          hint={profile.cms.version}
        />
        <StatCard
          icon={<ServerIcon />}
          label="Hosting"
          value={site.hosting ?? dash}
          hint={profile.hosting.asn}
        />
        <StatCard
          icon={<ServerIcon />}
          label="Server"
          value={profile.hosting.server ?? dash}
          hint={profile.hosting.language}
        />
        <StatCard
          icon={<LockIcon />}
          label="SSL"
          value={sslDays === null ? dash : `${sslDays} days`}
          hint={profile.tls.verified ? profile.tls.issuer : "not trusted"}
          tone={
            sslDays === null
              ? "default"
              : !profile.tls.verified || sslDays < 14
                ? "bad"
                : sslDays < 30
                  ? "warn"
                  : "good"
          }
        />
        <StatCard
          icon={<GlobeIcon />}
          label="Domain expires"
          value={shortDate(profile.domain.expires)}
          hint={profile.domain.registrar}
        />
        <StatCard
          icon={<ClockIcon />}
          label="Domain age"
          value={
            profile.domain.ageYears === null
              ? dash
              : `${profile.domain.ageYears} y`
          }
          hint={
            profile.domain.created ? `since ${profile.domain.created}` : null
          }
        />
        <StatCard
          icon={<HistoryIcon />}
          label="Design dated"
          value={profile.design.estimatedFrom ?? dash}
          hint={
            profile.design.estimatedFrom
              ? `${profile.design.confidence ?? "low"} confidence`
              : "no clues"
          }
        />
        <StatCard
          icon={<ActivityIcon />}
          label="Fetched"
          value={relativeTime(snapshot.fetchedAt)}
          hint={
            snapshot.durationMs === null
              ? null
              : `${Math.round(snapshot.durationMs / 1000)} s`
          }
        />
      </div>

      <div className="flex w-fit gap-1 rounded-lg bg-muted p-0.5 text-sm">
        {(["profile", "raw"] as const).map((value) => (
          <button
            key={value}
            onClick={() => setTab(value)}
            className={cn(
              "rounded-md px-3 py-1",
              tab === value
                ? "bg-background font-medium shadow-sm"
                : "text-muted-foreground"
            )}
          >
            {value === "profile" ? "Profile" : "Raw JSON"}
          </button>
        ))}
      </div>

      {tab === "profile" ? (
        <ProfileSections profile={profile} />
      ) : raw.data ? (
        <JsonView
          value={raw.data.raw}
          name={`${site.siteKey}-profile-${snapshot.id}.json`}
        />
      ) : (
        <div className="h-40 animate-pulse rounded-xl bg-muted/40" />
      )}
    </div>
  )
}
