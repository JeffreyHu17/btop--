import { useId } from 'react'

export type TrendLine = {
  key: string
  label: string
  color: string
  values: Array<number | null>
}

type MiniTrendChartProps = {
  lines: TrendLine[]
  emptyLabel: string
  ariaLabel: string
}

export function MiniTrendChart({ lines, emptyLabel, ariaLabel }: MiniTrendChartProps) {
  const chartId = useId().replace(/:/g, '')
  const usableLines = lines.filter((line) => line.values.some((value) => value != null))
  const pointCount = usableLines.reduce((max, line) => Math.max(max, line.values.length), 0)

  if (!usableLines.length || pointCount === 0) {
    return <div className="chart-empty">{emptyLabel}</div>
  }

  const width = 100
  const height = 44
  const paddingX = 4
  const paddingY = 4
  const step = pointCount <= 1 ? 0 : (width - paddingX * 2) / (pointCount - 1)

  return (
    <div className="mini-trend">
      <svg aria-label={ariaLabel} className="mini-trend-canvas" preserveAspectRatio="none" viewBox={`0 0 ${width} ${height}`}>
        <defs>
          {usableLines.map((line) => (
            <linearGradient id={`${chartId}-${line.key}`} key={line.key} x1="0" x2="1" y1="0" y2="0">
              <stop offset="0%" stopColor={line.color} stopOpacity="0.92" />
              <stop offset="100%" stopColor={line.color} stopOpacity="0.45" />
            </linearGradient>
          ))}
        </defs>
        <line className="sparkline-baseline" x1={paddingX} x2={width - paddingX} y1={height - paddingY} y2={height - paddingY} />
        {usableLines.map((line) => {
          const path = buildLinePath(line.values, width, height, paddingX, paddingY, step)
          if (!path) {
            return null
          }
          return (
            <path
              d={path}
              fill="none"
              key={line.key}
              stroke={`url(#${chartId}-${line.key})`}
              strokeLinecap="round"
              strokeLinejoin="round"
              strokeWidth="1.35"
            />
          )
        })}
      </svg>
      <div className="mini-trend-legend">
        {usableLines.map((line) => (
          <span className="mini-trend-chip" key={line.key}>
            <span className="mini-trend-dot" style={{ backgroundColor: line.color }} />
            {line.label}
          </span>
        ))}
      </div>
    </div>
  )
}

function buildLinePath(
  values: Array<number | null>,
  width: number,
  height: number,
  paddingX: number,
  paddingY: number,
  step: number,
): string {
  let started = false
  const commands: string[] = []

  values.forEach((value, index) => {
    if (value == null || !Number.isFinite(value)) {
      started = false
      return
    }

    const clamped = Math.min(Math.max(value, 0), 100)
    const x = values.length <= 1 ? width / 2 : paddingX + index * step
    const y = height - paddingY - (clamped / 100) * (height - paddingY * 2)
    commands.push(`${started ? 'L' : 'M'} ${x} ${y}`)
    started = true
  })

  return commands.join(' ')
}
