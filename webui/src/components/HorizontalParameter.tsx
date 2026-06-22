import { Box, Input, Slider, Typography } from '@mui/material'
import { useEffect, useEffectEvent, useRef, useState } from 'react'

import { useFineAdjustPointer } from '../hooks/useFineAdjustPointer'
import { useNumberInputAdjust } from '../hooks/useNumberInputAdjust'
import { useParam, useParamStore } from '../store/paramStore'

interface HorizontalParameterProps {
  paramId: number
  label: string
  min: number
  max: number
  unit?: string
  /** Display formatter receiving the scaled value. Default: toFixed(1). */
  format?: (scaled: number) => string
  /** Reset target (scaled) for Ctrl/Cmd + click. Omit to disable reset. */
  defaultValue?: number
  labelWidth?: number
  inputWidth?: number
}

const clamp01 = (t: number): number => (t < 0 ? 0 : t > 1 ? 1 : t)

// Horizontal slider + numeric input, linear scaled domain [min,max], wired to the
// iPlug2 paramStore. Visual layout/styling mirrors ZeroComp's HorizontalParameter;
// the data layer reads/writes normalized [0,1] via the store.
export function HorizontalParameter({
  paramId,
  label,
  min,
  max,
  unit,
  format,
  defaultValue,
  labelWidth = 64,
  inputWidth = 50,
}: HorizontalParameterProps) {
  const norm = useParam(paramId)
  const setFromUI = useParamStore((s) => s.setFromUI)
  const beginGesture = useParamStore((s) => s.beginGesture)
  const endGesture = useParamStore((s) => s.endGesture)

  const span = max - min || 1
  const value = min + norm * span // scaled
  const applyScaled = (scaled: number) => setFromUI(paramId, clamp01((scaled - min) / span))

  const [isDragging, setIsDragging] = useState(false)
  const [isEditing, setIsEditing] = useState(false)
  const [inputText, setInputText] = useState('')

  const formatted = format ? format(value) : value.toFixed(1)
  const displayInput = isEditing ? inputText : formatted

  const wheelStep = span * 0.01
  const wheelStepFine = span * 0.002

  // Slider wheel: register the listener once, read latest value via Effect Event.
  const stepFromCurrent = useEffectEvent((direction: 1 | -1, fine: boolean) => {
    applyScaled(value + (fine ? wheelStepFine : wheelStep) * direction)
  })
  const wheelRef = useRef<HTMLDivElement | null>(null)
  useEffect(() => {
    const el = wheelRef.current
    if (!el) return
    const onWheel = (e: WheelEvent) => {
      e.preventDefault()
      const direction: 1 | -1 = -e.deltaY > 0 ? 1 : -1
      const fine = e.shiftKey || e.ctrlKey || e.metaKey || e.altKey
      stepFromCurrent(direction, fine)
    }
    el.addEventListener('wheel', onWheel, { passive: false })
    return () => el.removeEventListener('wheel', onWheel as EventListener)
  }, [])

  // Modifier + drag = fine adjust; Ctrl/Cmd + click = reset (if defaultValue given).
  const fineStartRef = useRef(0)
  const handlePointerDownCapture = useFineAdjustPointer({
    orientation: 'horizontal',
    onReset: () => {
      if (defaultValue !== undefined) applyScaled(defaultValue)
    },
    onDragStart: () => {
      fineStartRef.current = value
      beginGesture(paramId)
    },
    onDragDelta: (deltaPx) => applyScaled(fineStartRef.current + deltaPx * wheelStepFine),
    onDragEnd: () => endGesture(paramId),
  })

  // Numeric input wheel + vertical drag.
  const inputElRef = useRef<HTMLInputElement | null>(null)
  const inputStartRef = useRef(0)
  useNumberInputAdjust(inputElRef, {
    onWheelStep: (direction, fine) =>
      applyScaled(value + (fine ? wheelStepFine : wheelStep) * direction),
    onDragStart: () => {
      inputStartRef.current = value
      beginGesture(paramId)
    },
    onDragDelta: (deltaY, fine) =>
      applyScaled(inputStartRef.current + deltaY * (fine ? wheelStepFine : wheelStep)),
    onDragEnd: () => endGesture(paramId),
  })

  return (
    <Box
      sx={{
        display: 'grid',
        gridTemplateColumns: `${labelWidth}px 1fr ${inputWidth}px`,
        alignItems: 'center',
        columnGap: 0.5,
        width: '100%',
      }}
    >
      <Typography
        variant="caption"
        sx={{ fontWeight: 500, fontSize: '0.72rem', color: 'text.primary', lineHeight: 1 }}
      >
        {label}
      </Typography>

      <Box
        ref={wheelRef}
        onPointerDownCapture={handlePointerDownCapture}
        sx={{ position: 'relative', display: 'flex', alignItems: 'center', minWidth: 0, px: '6px' }}
      >
        <Slider
          value={clamp01(norm)}
          onChange={(_e, v) => applyScaled(min + (v as number) * span)}
          onMouseDown={() => {
            if (!isDragging) {
              setIsDragging(true)
              beginGesture(paramId)
            }
          }}
          onChangeCommitted={() => {
            if (isDragging) {
              setIsDragging(false)
              endGesture(paramId)
            }
          }}
          min={0}
          max={1}
          step={0.001}
          valueLabelDisplay="off"
          sx={{
            width: '100%',
            padding: 0,
            height: 12,
            '@media (pointer: coarse)': { padding: 0 },
            '& .MuiSlider-thumb': { width: 12, height: 12, transition: 'opacity 80ms' },
            '& .MuiSlider-track': { height: 3, border: 'none' },
            '& .MuiSlider-rail': { height: 3, opacity: 0.5 },
          }}
        />
      </Box>

      <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'flex-end', gap: 0.5 }}>
        <Input
          className="block-host-shortcuts"
          inputRef={inputElRef}
          value={displayInput}
          onChange={(e) => setInputText(e.target.value)}
          onFocus={() => {
            setIsEditing(true)
            setInputText(formatted)
          }}
          onBlur={() => {
            setIsEditing(false)
            const parsed = Number.parseFloat(inputText)
            if (!Number.isNaN(parsed)) applyScaled(parsed)
          }}
          onKeyDown={(e) => {
            if (e.key === 'Enter') (e.target as HTMLInputElement).blur()
          }}
          disableUnderline
          sx={{
            '& input': {
              padding: '2px 3px',
              fontSize: '10px',
              textAlign: 'right',
              width: 26,
              backgroundColor: '#252525',
              border: '1px solid #404040',
              borderRadius: 2,
              fontFamily: '"Red Hat Mono", monospace',
            },
          }}
        />
        {unit && (
          <Typography
            variant="caption"
            sx={{
              fontSize: '10px',
              color: 'text.secondary',
              width: 14,
              textAlign: 'left',
              lineHeight: 1,
            }}
          >
            {unit}
          </Typography>
        )}
      </Box>
    </Box>
  )
}
