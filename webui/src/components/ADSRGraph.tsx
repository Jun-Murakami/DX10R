import { Box } from '@mui/material'
import { darken, lighten, useTheme } from '@mui/material/styles'
import { useEffect, useId, useRef, useState } from 'react'

import { useParamStore } from '../store/paramStore'

// ADSR polyline + draggable handles, adapted from Synth80's ADSRGraph.
// Drives the carrier envelope directly by paramIdx (no Upper/Lower tone resolution).
//   - Attack handle (peak): X = attack
//   - Decay/Sustain handle (corner): X = decay, Y = sustain
//   - Release handle (end): X = release
// Each phase gets an equal zone width (A2 / D2 / S-hold1 / R2); illustrative only.

const HANDLE_R = 4
const PAD = HANDLE_R + 1
const SUM = 7

interface Props {
  attackIdx: number
  decayIdx: number
  sustainIdx: number
  releaseIdx: number
  height?: number | string
}

function clamp01(v: number) {
  return v < 0 ? 0 : v > 1 ? 1 : v
}

export function ADSRGraph({ attackIdx, decayIdx, sustainIdx, releaseIdx, height = 60 }: Props) {
  const a = useParamStore((s) => s.values[attackIdx] ?? 0)
  const d = useParamStore((s) => s.values[decayIdx] ?? 0)
  const sus = useParamStore((s) => s.values[sustainIdx] ?? 0)
  const r = useParamStore((s) => s.values[releaseIdx] ?? 0)
  const setFromUI = useParamStore((s) => s.setFromUI)
  const beginGesture = useParamStore((s) => s.beginGesture)
  const endGesture = useParamStore((s) => s.endGesture)

  const theme = useTheme()
  const stroke = theme.palette.primary.main
  const handleFill = lighten(stroke, 0.4)
  const grid = darken(theme.palette.background.paper, 0.4)
  const fillGradientId = useId()

  const containerRef = useRef<HTMLDivElement | null>(null)
  const [size, setSize] = useState({ w: 200, h: 60 })
  useEffect(() => {
    const el = containerRef.current
    if (!el) return
    let rafId: number | null = null
    const apply = (w: number, h: number) => {
      if (rafId !== null) return
      rafId = requestAnimationFrame(() => {
        rafId = null
        setSize({ w: Math.max(1, w), h: Math.max(1, h) })
      })
    }
    const ro = new ResizeObserver((entries) => {
      const e = entries[0]
      if (!e) return
      apply(e.contentRect.width, e.contentRect.height)
    })
    ro.observe(el)
    const rect = el.getBoundingClientRect()
    apply(rect.width, rect.height)
    return () => {
      if (rafId !== null) cancelAnimationFrame(rafId)
      ro.disconnect()
    }
  }, [])

  const W = size.w
  const H = size.h
  const pathTop = PAD
  const pathBottom = H - PAD
  const pathHeight = pathBottom - pathTop
  const pathLeft = PAD
  const pathRight = W - PAD
  const innerW = Math.max(0, pathRight - pathLeft)

  const aZone = innerW * (2 / SUM)
  const dZone = innerW * (2 / SUM)
  const sHold = innerW * (1 / SUM)
  const rZone = innerW * (2 / SUM)

  const aX = pathLeft + aZone * a
  const dX = pathLeft + aZone + dZone * d
  const sStartX = dX
  const sEndX = sStartX + sHold
  const rX = sEndX + rZone * r
  const sY = pathTop + (1 - sus) * pathHeight

  const pathD = `M ${pathLeft.toFixed(2)} ${pathBottom} L ${aX.toFixed(2)} ${pathTop} L ${dX.toFixed(2)} ${sY.toFixed(2)} L ${sEndX.toFixed(2)} ${sY.toFixed(2)} L ${rX.toFixed(2)} ${pathBottom}`
  const fillD = `${pathD} L ${pathLeft.toFixed(2)} ${pathBottom} Z`

  function startDrag(
    e: React.PointerEvent<SVGCircleElement>,
    xMap: ((dx: number) => { idx: number; delta: number } | null) | null,
    yMap: ((dy: number) => { idx: number; delta: number } | null) | null,
  ) {
    e.stopPropagation()
    const startClientX = e.clientX
    const startClientY = e.clientY
    const startVals: Record<number, number> = {}
    const idxsToCapture = new Set<number>()

    const snapshot = (idx: number) => {
      if (!(idx in startVals)) {
        startVals[idx] = useParamStore.getState().values[idx] ?? 0
        idxsToCapture.add(idx)
      }
    }

    const probeX = xMap ? xMap(0) : null
    const probeY = yMap ? yMap(0) : null
    if (probeX) {
      snapshot(probeX.idx)
      beginGesture(probeX.idx)
    }
    if (probeY) {
      snapshot(probeY.idx)
      if (probeY.idx !== probeX?.idx) beginGesture(probeY.idx)
    }

    const target = e.currentTarget
    target.setPointerCapture(e.pointerId)

    const onMove = (ev: PointerEvent) => {
      const dx = ev.clientX - startClientX
      const dy = ev.clientY - startClientY
      const xRes = xMap ? xMap(dx) : null
      const yRes = yMap ? yMap(dy) : null
      if (xRes) {
        const start = startVals[xRes.idx] ?? 0
        setFromUI(xRes.idx, clamp01(start + xRes.delta))
      }
      if (yRes) {
        const start = startVals[yRes.idx] ?? 0
        setFromUI(yRes.idx, clamp01(start + yRes.delta))
      }
    }
    const onUp = (ev: PointerEvent) => {
      target.releasePointerCapture(ev.pointerId)
      target.removeEventListener('pointermove', onMove)
      target.removeEventListener('pointerup', onUp)
      target.removeEventListener('pointercancel', onUp)
      for (const idx of idxsToCapture) endGesture(idx)
    }
    target.addEventListener('pointermove', onMove)
    target.addEventListener('pointerup', onUp)
    target.addEventListener('pointercancel', onUp)
  }

  return (
    <Box
      ref={containerRef}
      sx={{
        width: '100%',
        height: typeof height === 'number' ? `${height}px` : height,
        position: 'relative',
        overflow: 'hidden',
      }}
    >
      <svg
        width={W}
        height={H}
        viewBox={`0 0 ${W} ${H}`}
        preserveAspectRatio="none"
        style={{ position: 'absolute', top: 0, left: 0, display: 'block', userSelect: 'none' }}
        aria-label="ADSR envelope graph"
      >
        <defs>
          <linearGradient
            id={fillGradientId}
            gradientUnits="userSpaceOnUse"
            x1={0}
            y1={pathTop}
            x2={0}
            y2={pathBottom}
          >
            <stop offset="0%" stopColor={stroke} stopOpacity={0.35} />
            <stop offset="100%" stopColor={stroke} stopOpacity={0.09} />
          </linearGradient>
        </defs>
        <line
          x1={pathLeft + aZone}
          y1={pathTop}
          x2={pathLeft + aZone}
          y2={pathBottom}
          stroke={grid}
          strokeWidth={0.5}
        />
        <line
          x1={pathLeft + aZone + dZone}
          y1={pathTop}
          x2={pathLeft + aZone + dZone}
          y2={pathBottom}
          stroke={grid}
          strokeWidth={0.5}
        />
        <line
          x1={pathLeft + aZone + dZone + sHold}
          y1={pathTop}
          x2={pathLeft + aZone + dZone + sHold}
          y2={pathBottom}
          stroke={grid}
          strokeWidth={0.5}
        />
        <path d={fillD} fill={`url(#${fillGradientId})`} stroke="none" />
        <path d={pathD} fill="none" stroke={stroke} strokeWidth={1.5} strokeLinejoin="round" />

        <circle
          cx={aX}
          cy={pathTop}
          r={HANDLE_R}
          fill={handleFill}
          stroke={stroke}
          strokeWidth={1.2}
          style={{ cursor: 'ew-resize' }}
          onPointerDown={(e) => startDrag(e, (dx) => ({ idx: attackIdx, delta: dx / aZone }), null)}
        />
        <circle
          cx={dX}
          cy={sY}
          r={HANDLE_R}
          fill={handleFill}
          stroke={stroke}
          strokeWidth={1.2}
          style={{ cursor: 'move' }}
          onPointerDown={(e) =>
            startDrag(
              e,
              (dx) => ({ idx: decayIdx, delta: dx / dZone }),
              (dy) => ({ idx: sustainIdx, delta: -dy / pathHeight }),
            )
          }
        />
        <circle
          cx={rX}
          cy={pathBottom}
          r={HANDLE_R}
          fill={handleFill}
          stroke={stroke}
          strokeWidth={1.2}
          style={{ cursor: 'ew-resize' }}
          onPointerDown={(e) =>
            startDrag(e, (dx) => ({ idx: releaseIdx, delta: dx / rZone }), null)
          }
        />
      </svg>
    </Box>
  )
}
