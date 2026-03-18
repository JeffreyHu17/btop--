import type { Locale } from './i18n'

const dateTimeFormatters = new Map<Locale, Intl.DateTimeFormat>()
const timeFormatters = new Map<Locale, Intl.DateTimeFormat>()
const dateFormatters = new Map<Locale, Intl.DateTimeFormat>()
const compactFormatters = new Map<Locale, Intl.NumberFormat>()
const oneDecimalFormatters = new Map<Locale, Intl.NumberFormat>()
const integerFormatters = new Map<Locale, Intl.NumberFormat>()
const relativeTimeFormatters = new Map<Locale, Intl.RelativeTimeFormat>()

function getDateTimeFormatter(locale: Locale) {
  if (!dateTimeFormatters.has(locale)) {
    dateTimeFormatters.set(
      locale,
      new Intl.DateTimeFormat(locale, {
        month: 'short',
        day: 'numeric',
        hour: 'numeric',
        minute: '2-digit',
      }),
    )
  }
  return dateTimeFormatters.get(locale)!
}

function getTimeFormatter(locale: Locale) {
  if (!timeFormatters.has(locale)) {
    timeFormatters.set(
      locale,
      new Intl.DateTimeFormat(locale, {
        hour: 'numeric',
        minute: '2-digit',
      }),
    )
  }
  return timeFormatters.get(locale)!
}

function getDateFormatter(locale: Locale) {
  if (!dateFormatters.has(locale)) {
    dateFormatters.set(
      locale,
      new Intl.DateTimeFormat(locale, {
        month: 'short',
        day: 'numeric',
      }),
    )
  }
  return dateFormatters.get(locale)!
}

function getCompactFormatter(locale: Locale) {
  if (!compactFormatters.has(locale)) {
    compactFormatters.set(
      locale,
      new Intl.NumberFormat(locale, {
        notation: 'compact',
        maximumFractionDigits: 1,
      }),
    )
  }
  return compactFormatters.get(locale)!
}

function getOneDecimalFormatter(locale: Locale) {
  if (!oneDecimalFormatters.has(locale)) {
    oneDecimalFormatters.set(locale, new Intl.NumberFormat(locale, { maximumFractionDigits: 1 }))
  }
  return oneDecimalFormatters.get(locale)!
}

function getIntegerFormatter(locale: Locale) {
  if (!integerFormatters.has(locale)) {
    integerFormatters.set(locale, new Intl.NumberFormat(locale, { maximumFractionDigits: 0 }))
  }
  return integerFormatters.get(locale)!
}

function getRelativeTimeFormatter(locale: Locale) {
  if (!relativeTimeFormatters.has(locale)) {
    relativeTimeFormatters.set(locale, new Intl.RelativeTimeFormat(locale, { numeric: 'auto' }))
  }
  return relativeTimeFormatters.get(locale)!
}

export function formatDateTime(value: string | null | undefined, locale: Locale): string {
  if (!value) {
    return locale === 'zh-CN' ? '从未' : 'Never'
  }

  const date = new Date(value)
  if (Number.isNaN(date.getTime())) {
    return value
  }

  return getDateTimeFormatter(locale).format(date)
}

export function formatChartLabel(value: string | null | undefined, locale: Locale): string {
  if (!value) {
    return '--'
  }

  const date = new Date(value)
  if (Number.isNaN(date.getTime())) {
    return value
  }

  const now = new Date()
  const sameDay =
    date.getFullYear() === now.getFullYear() &&
    date.getMonth() === now.getMonth() &&
    date.getDate() === now.getDate()

  return sameDay
    ? getTimeFormatter(locale).format(date)
    : `${getDateFormatter(locale).format(date)} ${getTimeFormatter(locale).format(date)}`
}

export function formatRelativeTime(value: string | null | undefined, locale: Locale): string {
  if (!value) {
    return locale === 'zh-CN' ? '从未' : 'Never'
  }

  const date = new Date(value)
  if (Number.isNaN(date.getTime())) {
    return value
  }

  const diffSeconds = Math.round((date.getTime() - Date.now()) / 1000)
  const absSeconds = Math.abs(diffSeconds)
  const formatter = getRelativeTimeFormatter(locale)

  if (absSeconds < 10) {
    return formatter.format(0, 'second')
  }

  const units: Array<[Intl.RelativeTimeFormatUnit, number]> = [
    ['day', 86400],
    ['hour', 3600],
    ['minute', 60],
    ['second', 1],
  ]

  for (const [unit, unitSeconds] of units) {
    if (absSeconds >= unitSeconds || unit === 'second') {
      return formatter.format(Math.round(diffSeconds / unitSeconds), unit)
    }
  }

  return value
}

export function formatBytes(value: number | null | undefined, locale: Locale): string {
  if (value == null || !Number.isFinite(value)) {
    return '--'
  }

  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
  let next = Math.abs(value)
  let unitIndex = 0

  while (next >= 1024 && unitIndex < units.length - 1) {
    next /= 1024
    unitIndex += 1
  }

  const signed = value < 0 ? -next : next
  const formatter = unitIndex === 0 ? getIntegerFormatter(locale) : getOneDecimalFormatter(locale)
  return `${formatter.format(signed)} ${units[unitIndex]}`
}

export function formatThroughput(value: number | null | undefined, locale: Locale): string {
  const formatted = formatBytes(value, locale)
  if (formatted === '--') {
    return formatted
  }
  return locale === 'zh-CN' ? `${formatted}/秒` : `${formatted}/s`
}

export function formatPercent(value: number | null | undefined, locale: Locale): string {
  if (value == null || !Number.isFinite(value)) {
    return '--'
  }
  return `${getOneDecimalFormatter(locale).format(value)}%`
}

export function formatNumber(value: number | null | undefined, locale: Locale): string {
  if (value == null || !Number.isFinite(value)) {
    return '--'
  }
  return Math.abs(value) >= 1000 ? getCompactFormatter(locale).format(value) : getOneDecimalFormatter(locale).format(value)
}

export function formatDuration(value: number | null | undefined, locale: Locale): string {
  if (value == null || !Number.isFinite(value)) {
    return '--'
  }

  const totalSeconds = Math.round(value)
  const days = Math.floor(totalSeconds / 86400)
  const hours = Math.floor((totalSeconds % 86400) / 3600)
  const minutes = Math.floor((totalSeconds % 3600) / 60)

  if (locale === 'zh-CN') {
    if (days > 0) return `${days}天 ${hours}小时`
    if (hours > 0) return `${hours}小时 ${minutes}分钟`
    if (minutes > 0) return `${minutes}分钟`
    return `${totalSeconds}秒`
  }

  if (days > 0) return `${days}d ${hours}h`
  if (hours > 0) return `${hours}h ${minutes}m`
  if (minutes > 0) return `${minutes}m`
  return `${totalSeconds}s`
}

export function formatMetricValue(metricPath: string, value: number | null | undefined, locale: Locale): string {
  if (metricPath.includes('percent')) {
    return formatPercent(value, locale)
  }
  if (metricPath.includes('bytes_per_second') || metricPath.includes('throughput')) {
    return formatThroughput(value, locale)
  }
  if (metricPath.includes('bytes')) {
    return formatBytes(value, locale)
  }
  if (metricPath.includes('seconds')) {
    return formatDuration(value, locale)
  }
  return formatNumber(value, locale)
}

export function formatWindowLabel(minutes: number, locale: Locale): string {
  if (minutes < 60) {
    return locale === 'zh-CN' ? `${minutes} 分钟` : `${minutes}m`
  }
  if (minutes < 1440) {
    const hours = Math.round(minutes / 60)
    return locale === 'zh-CN' ? `${hours} 小时` : `${hours}h`
  }
  const days = Math.round(minutes / 1440)
  return locale === 'zh-CN' ? `${days} 天` : `${days}d`
}
