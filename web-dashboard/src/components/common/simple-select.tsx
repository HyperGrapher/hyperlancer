import type { ReactNode } from "react"

import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

export type Option = { value: string; label: ReactNode }

/** Single-value select over a flat option list. */
export function SimpleSelect({
  value,
  onChange,
  options,
  placeholder,
  className,
  size,
}: {
  value: string | null
  onChange: (value: string | null) => void
  options: Option[]
  placeholder?: string
  className?: string
  size?: "sm" | "default"
}) {
  return (
    <Select
      items={options}
      value={value}
      onValueChange={(next) => onChange(next as string | null)}
    >
      <SelectTrigger className={className} size={size}>
        <SelectValue placeholder={placeholder} />
      </SelectTrigger>
      <SelectContent>
        {options.map((option) => (
          <SelectItem key={option.value} value={option.value}>
            {option.label}
          </SelectItem>
        ))}
      </SelectContent>
    </Select>
  )
}
