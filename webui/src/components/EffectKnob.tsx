// Effect-slot knob (ported from Synth-80). Reuses the rotary Knob primitive and
// builds the label / value from effect-specific metadata (dx10r-effects.ts).
//
// gestures:
//   - 200px drag = full 0..1 sweep (Shift/Ctrl/Cmd = 0.2x fine)
//   - 1 wheel notch = ±2% (±0.4% fine)
//   - Ctrl/Cmd+Click resets to the param's effect-specific default

import { Box, Typography } from '@mui/material'
import { useEffect, useRef } from 'react'

import { type EffectKnobMeta, formatEffectValue } from '../dx10r-effects'
import { useParamStore } from '../store/paramStore'
import { Knob } from './Knob'

const PX_PER_FULL_RANGE = 200
const FINE_FACTOR = 0.2
const WHEEL_NORM_STEP = 0.02
const MOVE_THRESHOLD_PX = 3

function clamp01(v: number) {
  return v < 0 ? 0 : v > 1 ? 1 : v
}

interface Props {
  /** real IParam idx (already computed by the caller). */
  paramIdx: number
  meta: EffectKnobMeta
  size?: number
}

export function EffectKnob({ paramIdx, meta, size = 36 }: Props) {
  const value = useParamStore((s) => s.values[paramIdx] ?? 0)
  const setFromUI = useParamStore((s) => s.setFromUI)
  const beginGesture = useParamStore((s) => s.beginGesture)
  const endGesture = useParamStore((s) => s.endGesture)

  const wrapRef = useRef<HTMLDivElement | null>(null)
  const valueRef = useRef(value)
  valueRef.current = value
  const anchorRef = useRef<{ startY: number; startNorm: number; moved: boolean } | null>(null)

  useEffect(() => {
    const el = wrapRef.current
    if (!el) return
    const handleWheel = (e: WheelEvent) => {
      e.preventDefault()
      const fine = e.ctrlKey || e.metaKey || e.shiftKey
      const dir = e.deltaY < 0 ? 1 : -1
      const step = WHEEL_NORM_STEP * (fine ? FINE_FACTOR : 1) * dir
      setFromUI(paramIdx, clamp01(valueRef.current + step))
    }
    el.addEventListener('wheel', handleWheel, { passive: false })
    return () => el.removeEventListener('wheel', handleWheel)
  }, [paramIdx, setFromUI])

  const onPointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    wrapRef.current?.setPointerCapture(e.pointerId)
    anchorRef.current = { startY: e.clientY, startNorm: value, moved: false }
    beginGesture(paramIdx)
    e.preventDefault()
  }

  const onPointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    const a = anchorRef.current
    if (!a) return
    const dy = e.clientY - a.startY
    if (!a.moved && Math.abs(dy) < MOVE_THRESHOLD_PX) return
    a.moved = true
    const fine = e.ctrlKey || e.metaKey || e.shiftKey
    const rate = fine ? FINE_FACTOR : 1.0
    const next = clamp01(a.startNorm + (-dy / PX_PER_FULL_RANGE) * rate)
    setFromUI(paramIdx, next)
  }

  const endDrag = (e: React.PointerEvent<HTMLDivElement>) => {
    const a = anchorRef.current
    if (!a) return
    wrapRef.current?.releasePointerCapture(e.pointerId)
    const wasClick = !a.moved
    anchorRef.current = null
    endGesture(paramIdx)
    if (wasClick && (e.ctrlKey || e.metaKey)) {
      setFromUI(paramIdx, meta.defaultValue ?? 0)
    }
  }

  const display = formatEffectValue(meta, value)

  return (
    <Box
      sx={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        userSelect: 'none',
        height: '100%',
        color: '#1a242b', // LCD navy ink
      }}
    >
      <Typography
        sx={{
          fontSize: 10,
          fontWeight: 700,
          letterSpacing: '0.08em',
          textTransform: 'uppercase',
          textAlign: 'center',
          lineHeight: 1.1,
          whiteSpace: 'pre',
          minHeight: '2.2em',
          display: 'flex',
          flexDirection: 'column',
          justifyContent: 'flex-start',
          alignItems: 'center',
          color: '#1a242b',
        }}
      >
        {meta.label}
      </Typography>
      <Box sx={{ flex: 1 }} />
      <div
        ref={wrapRef}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={endDrag}
        onPointerCancel={endDrag}
        style={{ touchAction: 'none', cursor: 'ns-resize' }}
      >
        <Knob
          value={value}
          size={size}
          color="#2c3a45"
          trackColor="rgba(0,0,0,0.18)"
          bipolar={meta.bipolarCenter !== undefined}
        />
      </div>
      <Typography
        variant="caption"
        sx={{
          fontSize: 10,
          fontFamily: '"Red Hat Mono", monospace',
          color: '#000',
          fontWeight: 700,
          textAlign: 'center',
          lineHeight: 1.1,
          mt: `-${Math.max(0, Math.round(size * 0.225 - 2))}px`,
          minHeight: '1.2em',
          whiteSpace: 'nowrap',
        }}
      >
        {display}
      </Typography>
      <Box sx={{ flex: 2 }} />
    </Box>
  )
}
