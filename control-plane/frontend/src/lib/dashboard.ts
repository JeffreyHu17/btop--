import type { MetricPayload } from './types'

export function getMetricValue(payload: MetricPayload | null | undefined, metricPath: string): unknown {
  if (!payload || !metricPath) {
    return null
  }

  return metricPath.split('.').reduce<unknown>((current, segment) => {
    if (!current || typeof current !== 'object') {
      return null
    }

    const record = current as Record<string, unknown>
    if (Array.isArray(current) && /^\d+$/.test(segment)) {
      return current[Number(segment)] ?? null
    }
    return record[segment] ?? null
  }, payload)
}

export function getNumericMetric(payload: MetricPayload | null | undefined, metricPath: string): number | null {
  const value = getMetricValue(payload, metricPath)
  if (typeof value === 'number' && Number.isFinite(value)) {
    return value
  }
  if (typeof value === 'string') {
    const parsed = Number(value)
    return Number.isFinite(parsed) ? parsed : null
  }
  return null
}

export function getTextMetric(payload: MetricPayload | null | undefined, metricPath: string): string | null {
  const value = getMetricValue(payload, metricPath)
  if (typeof value === 'string') {
    return value
  }
  if (typeof value === 'number' || typeof value === 'boolean') {
    return String(value)
  }
  return null
}

export function extractTimestamp(payload: MetricPayload | null | undefined): string | null {
  if (!payload) {
    return null
  }

  const rawTimestamp = payload.timestamp ?? payload.received_at
  return typeof rawTimestamp === 'string' ? rawTimestamp : null
}
