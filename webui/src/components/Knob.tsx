// Rotary knob primitive (ported from Synth-80). Draws an 8 o'clock → 4 o'clock
// 240° sweep: outer track + inner value arc. Interaction lives in EffectKnob.
//
// bipolar=true treats normalized 0.5 as center (12 o'clock) and extends the arc
// toward the current value only; at exact center the arc vanishes so a small ▼
// marker is drawn straight up.

import { useTheme } from '@mui/material/styles'

interface Props {
  value: number // 0..1
  size?: number
  thickness?: number
  /** Defaults to theme.palette.primary.main. */
  color?: string
  trackColor?: string
  disabled?: boolean
  centerLabel?: string
  /** true → center-fill (paint from 0.5 toward the current value). */
  bipolar?: boolean
}

function polar(cx: number, cy: number, r: number, clockDeg: number) {
  const a = ((clockDeg - 90) * Math.PI) / 180
  return { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) }
}

function arcPath(cx: number, cy: number, r: number, startClock: number, sweepDeg: number) {
  const endClock = startClock + sweepDeg
  const start = polar(cx, cy, r, startClock)
  const end = polar(cx, cy, r, endClock)
  const largeArc = sweepDeg > 180 ? 1 : 0
  return `M ${start.x.toFixed(2)} ${start.y.toFixed(2)} A ${r} ${r} 0 ${largeArc} 1 ${end.x.toFixed(2)} ${end.y.toFixed(2)}`
}

const BIPOLAR_CENTER_EPS = 0.0001

export function Knob({
  value,
  size = 38,
  thickness = 5,
  color,
  trackColor = 'rgba(255,255,255,0.10)',
  disabled = false,
  centerLabel,
  bipolar = false,
}: Props) {
  const theme = useTheme()
  const resolvedColor = color ?? theme.palette.primary.main
  const v = Math.max(0, Math.min(1, value))
  const r = (size - thickness) / 2 - 1
  const cx = size / 2
  const cy = size / 2

  // 8 o'clock start, 240° sweep. 12 o'clock (top) is the center.
  const START_CLOCK = 240
  const TOTAL_SWEEP = 240
  const CENTER_CLOCK = START_CLOCK + TOTAL_SWEEP / 2 // = 360 (= 0, 12 o'clock)

  const trackD = arcPath(cx, cy, r, START_CLOCK, TOTAL_SWEEP)

  let valueD: string | null = null
  const isAtCenter = bipolar && Math.abs(v - 0.5) < BIPOLAR_CENTER_EPS
  if (bipolar) {
    if (!isAtCenter) {
      const halfSweep = TOTAL_SWEEP / 2
      if (v > 0.5) {
        const sweep = halfSweep * (v - 0.5) * 2
        valueD = arcPath(cx, cy, r, CENTER_CLOCK, sweep)
      } else {
        const sweep = halfSweep * (0.5 - v) * 2
        valueD = arcPath(cx, cy, r, CENTER_CLOCK - sweep, sweep)
      }
    }
  } else if (v > 0.001) {
    valueD = arcPath(cx, cy, r, START_CLOCK, TOTAL_SWEEP * v)
  }

  const opacity = disabled ? 0.35 : 1
  const strokeColor = disabled ? '#777' : resolvedColor

  const markerColor = isAtCenter ? strokeColor : 'rgba(255,255,255,0.18)'
  const markerW = Math.max(4, Math.round(size * 0.12))
  const markerH = Math.max(3, Math.round(size * 0.09))
  const MARKER_GAP = 3
  const arcOuterTopY = cy - r - thickness / 2
  const markerTipY = arcOuterTopY - MARKER_GAP
  const markerBaseY = markerTipY - markerH
  const markerPoints = `${cx - markerW / 2},${markerBaseY} ${cx + markerW / 2},${markerBaseY} ${cx},${markerTipY}`

  return (
    <svg
      width={size}
      height={size}
      style={{ opacity, display: 'block', overflow: 'visible' }}
      role="img"
      aria-label="knob"
    >
      <path
        d={trackD}
        stroke={trackColor}
        strokeWidth={thickness}
        fill="none"
        strokeLinecap="round"
      />
      {valueD && (
        <path
          d={valueD}
          stroke={strokeColor}
          strokeWidth={thickness}
          fill="none"
          strokeLinecap="butt"
        />
      )}
      {bipolar && <polygon points={markerPoints} fill={markerColor} />}
      {centerLabel && (
        <text
          x={cx}
          y={cy}
          textAnchor="middle"
          dominantBaseline="central"
          style={{
            fill: disabled ? '#777' : '#ddd',
            fontSize: size * 0.28,
            fontFamily: 'inherit',
            fontWeight: 500,
          }}
        >
          {centerLabel}
        </text>
      )}
    </svg>
  )
}
