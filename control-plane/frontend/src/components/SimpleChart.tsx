import { useId } from 'react'

export type ChartSample = {
  label: string
  value: number
}

type SimpleChartProps = {
  samples: ChartSample[]
  accent?: string
  emptyLabel?: string
  ariaLabel?: string
}

export function SimpleChart({
  samples,
  accent = '#73e0ce',
  emptyLabel = 'No samples in this window.',
  ariaLabel = 'Metric history',
}: SimpleChartProps) {
  const gradientId = useId().replace(/:/g, '')

  if (samples.length === 0) {
    return <div className="chart-empty">{emptyLabel}</div>
  }

  const width = 100
  const height = 40
  const paddingX = 4
  const paddingY = 4
  const minValue = Math.min(...samples.map((sample) => sample.value))
  const maxValue = Math.max(...samples.map((sample) => sample.value))
  const range = maxValue - minValue || Math.max(Math.abs(maxValue), 1)
  const step = samples.length === 1 ? 0 : (width - paddingX * 2) / (samples.length - 1)

  const points = samples.map((sample, index) => {
    const x = samples.length === 1 ? width / 2 : paddingX + index * step
    const relative = (sample.value - minValue) / range
    const y = height - paddingY - relative * (height - paddingY * 2)
    return { x, y }
  })

  const polyline = points.map((point) => `${point.x},${point.y}`).join(' ')
  const areaPath = [
    `M ${points[0].x} ${height - paddingY}`,
    ...points.map((point) => `L ${point.x} ${point.y}`),
    `L ${points[points.length - 1].x} ${height - paddingY}`,
    'Z',
  ].join(' ')

  return (
    <div className="simple-chart">
      <svg aria-label={ariaLabel} className="sparkline" preserveAspectRatio="none" viewBox={`0 0 ${width} ${height}`}>
        <defs>
          <linearGradient id={gradientId} x1="0" x2="0" y1="0" y2="1">
            <stop offset="0%" stopColor={accent} stopOpacity="0.32" />
            <stop offset="100%" stopColor={accent} stopOpacity="0.04" />
          </linearGradient>
        </defs>
        <line className="sparkline-baseline" x1={paddingX} x2={width - paddingX} y1={height - paddingY} y2={height - paddingY} />
        <path d={areaPath} fill={`url(#${gradientId})`} />
        <polyline fill="none" points={polyline} stroke={accent} strokeLinecap="round" strokeLinejoin="round" strokeWidth="1.1" />
      </svg>
      <div className="chart-axis">
        <span>{samples[0].label}</span>
        <span>{samples[samples.length - 1].label}</span>
      </div>
    </div>
  )
}
