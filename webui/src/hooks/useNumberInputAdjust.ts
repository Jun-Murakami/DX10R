import type React from 'react'
import { useEffect, useEffectEvent } from 'react'

export interface NumberInputAdjustOptions {
  /** wheel: direction (+1/-1) and fine flag (Ctrl/Cmd/Shift/Alt held) */
  onWheelStep: (direction: 1 | -1, fine: boolean) => void
  /** drag start (past threshold) */
  onDragStart: () => void
  /** drag move: cumulative px (up is +) and fine flag */
  onDragDelta: (deltaY: number, fine: boolean) => void
  /** drag end */
  onDragEnd: () => void
  /** click only (no move): default focuses + selects. Override to customise. */
  onClickEditFallback?: (el: HTMLElement) => void
  moveThreshold?: number
}

// Adds wheel + vertical-drag value editing to a numeric input. While the input is
// focused (text editing) it does nothing so native caret/selection works.
export function useNumberInputAdjust(
  inputRef: React.RefObject<HTMLElement | null>,
  options: NumberInputAdjustOptions,
) {
  const latest = useEffectEvent(() => options)

  useEffect(() => {
    const el = inputRef.current
    if (!el) return

    const isEditing = () => document.activeElement === el

    const onWheel = (e: WheelEvent) => {
      if (isEditing()) return
      e.preventDefault()
      const direction = -e.deltaY > 0 ? 1 : -1
      const fine = e.shiftKey || e.ctrlKey || e.metaKey || e.altKey
      latest().onWheelStep(direction, fine)
    }

    const onPointerDown = (e: PointerEvent) => {
      if (isEditing()) return
      if (e.button !== 0) return

      const startX = e.clientX
      const startY = e.clientY
      const pointerId = e.pointerId
      let moved = false
      const threshold = latest().moveThreshold ?? 3

      e.preventDefault()

      const onMove = (ev: PointerEvent) => {
        if (ev.pointerId !== pointerId) return
        const dx = ev.clientX - startX
        const dy = ev.clientY - startY
        if (!moved && Math.hypot(dx, dy) >= threshold) {
          moved = true
          latest().onDragStart()
        }
        if (moved) {
          const fine = ev.shiftKey || ev.ctrlKey || ev.metaKey || ev.altKey
          latest().onDragDelta(-dy, fine)
        }
      }

      const onUp = (ev: PointerEvent) => {
        if (ev.pointerId !== pointerId) return
        cleanup()
        if (moved) {
          latest().onDragEnd()
        } else {
          const fallback = latest().onClickEditFallback
          if (fallback) {
            fallback(el)
          } else {
            el.focus()
            if (el instanceof HTMLInputElement) el.select()
          }
        }
      }

      const onCancel = (ev: PointerEvent) => {
        if (ev.pointerId !== pointerId) return
        cleanup()
        if (moved) latest().onDragEnd()
      }

      const cleanup = () => {
        document.removeEventListener('pointermove', onMove, true)
        document.removeEventListener('pointerup', onUp, true)
        document.removeEventListener('pointercancel', onCancel, true)
      }

      document.addEventListener('pointermove', onMove, true)
      document.addEventListener('pointerup', onUp, true)
      document.addEventListener('pointercancel', onCancel, true)
    }

    el.addEventListener('wheel', onWheel, { passive: false })
    el.addEventListener('pointerdown', onPointerDown)

    return () => {
      el.removeEventListener('wheel', onWheel as EventListener)
      el.removeEventListener('pointerdown', onPointerDown)
    }
  }, [inputRef])
}
