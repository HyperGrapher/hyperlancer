import { CheckIcon, CopyIcon } from "lucide-react"
import { useState, type ReactNode } from "react"
import { toast } from "sonner"
import { cn } from "cn"

import { Button } from "@/components/ui/button"

export function CopyButton({
  value,
  className,
}: {
  value: string
  className?: string
}) {
  const [copied, setCopied] = useState(false)

  async function copy() {
    try {
      if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(value)
      } else {
        // http://dashboard.test is not a secure context, so navigator.clipboard does not exist there.
        const area = document.createElement("textarea")
        area.value = value
        area.style.position = "fixed"
        area.style.opacity = "0"
        document.body.appendChild(area)
        area.select()
        const ok = document.execCommand("copy")
        area.remove()
        if (!ok) throw new Error("copy failed")
      }
      setCopied(true)
      setTimeout(() => setCopied(false), 1200)
    } catch {
      toast.error("Could not copy to the clipboard")
    }
  }

  return (
    <Button
      variant="ghost"
      size="icon-xs"
      className={cn("text-muted-foreground", className)}
      onClick={copy}
      aria-label="Copy"
    >
      {copied ? <CheckIcon className="text-emerald-400" /> : <CopyIcon />}
    </Button>
  )
}

export function PageHeader({
  title,
  description,
  actions,
}: {
  title: ReactNode
  description?: ReactNode
  actions?: ReactNode
}) {
  return (
    <div className="flex flex-wrap items-end justify-between gap-3 pb-5">
      <div className="min-w-0">
        <h1 className="truncate text-xl font-semibold tracking-tight">
          {title}
        </h1>
        {description && (
          <p className="mt-1 text-sm text-muted-foreground">{description}</p>
        )}
      </div>
      {actions && (
        <div className="flex flex-wrap items-center gap-2">{actions}</div>
      )}
    </div>
  )
}

export function EmptyState({
  icon,
  title,
  description,
  action,
}: {
  icon: ReactNode
  title: string
  description?: string
  action?: ReactNode
}) {
  return (
    <div className="flex flex-col items-center justify-center rounded-xl border border-dashed border-border px-6 py-12 text-center">
      <div className="mb-3 text-muted-foreground [&_svg]:size-8">{icon}</div>
      <p className="font-medium">{title}</p>
      {description && (
        <p className="mt-1 max-w-sm text-sm text-muted-foreground">
          {description}
        </p>
      )}
      {action && <div className="mt-4">{action}</div>}
    </div>
  )
}

/** Labelled value row used throughout the profile cards. */
export function KeyValue({
  label,
  value,
  copy,
  mono,
}: {
  label: string
  value: ReactNode
  copy?: string
  mono?: boolean
}) {
  return (
    <div className="flex items-start justify-between gap-4 py-1.5 text-sm">
      <dt className="shrink-0 text-muted-foreground">{label}</dt>
      <dd
        className={cn(
          "flex min-w-0 items-center gap-1 text-right break-all",
          mono && "font-mono text-xs"
        )}
      >
        {value ?? <span className="text-muted-foreground">—</span>}
        {copy && <CopyButton value={copy} />}
      </dd>
    </div>
  )
}

export function StatCard({
  label,
  value,
  hint,
  tone = "default",
  icon,
}: {
  label: string
  value: ReactNode
  hint?: ReactNode
  tone?: "default" | "good" | "warn" | "bad"
  icon?: ReactNode
}) {
  const toneClass = {
    default: "",
    good: "text-emerald-300",
    warn: "text-amber-300",
    bad: "text-rose-300",
  }[tone]
  return (
    <div className="rounded-xl border border-border bg-card/60 p-3">
      <div className="flex items-center gap-1.5 text-xs text-muted-foreground [&_svg]:size-3.5">
        {icon}
        {label}
      </div>
      <div className={cn("mt-1 truncate text-lg font-semibold", toneClass)}>
        {value}
      </div>
      {hint && (
        <div className="truncate text-xs text-muted-foreground">{hint}</div>
      )}
    </div>
  )
}
