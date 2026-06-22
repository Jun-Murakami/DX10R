import type React from 'react'

export interface FineAdjustPointerOptions {
  /** Ctrl/Cmd + click (no move) resets to default. Shift-only click is a no-op. */
  onReset: () => void
  /** Fired once when a modifier-drag crosses the move threshold. */
  onDragStart: () => void
  /** Modifier-drag: cumulative px from drag start. vertical: up is +, horizontal: right is +. */
  onDragDelta: (deltaPx: number) => void
  /** Fired on pointerup / pointercancel of a modifier-drag. */
  onDragEnd: () => void
  orientation?: 'vertical' | 'horizontal'
  /** px threshold to distinguish click from drag. default 3 */
  moveThreshold?: number
}

// Modifier (Ctrl / Cmd / Shift) + pointer = fine-adjust mode.
//  - click (no move): Ctrl/Cmd → onReset; Shift-only → no-op
//  - drag (past threshold): any modifier → onDragStart → onDragDelta → onDragEnd
//  - no-modifier drag is left to the MUI Slider (we do nothing).
export function useFineAdjustPointer(options: FineAdjustPointerOptions) {
  return (e: React.PointerEvent) => {
    const ctrl = e.ctrlKey || e.metaKey
    const shift = e.shiftKey
    if (!ctrl && !shift) return

    e.preventDefault()
    e.stopPropagation()
    e.nativeEvent.stopImmediatePropagation()

    const startX = e.clientX
    const startY = e.clientY
    const pointerId = e.pointerId
    let moved = false
    const threshold = options.moveThreshold ?? 3
    const orientation = options.orientation ?? 'vertical'

    const onMove = (ev: PointerEvent) => {
      if (ev.pointerId !== pointerId) return
      const dx = ev.clientX - startX
      const dy = ev.clientY - startY
      if (!moved && Math.hypot(dx, dy) >= threshold) {
        moved = true
        options.onDragStart()
      }
      if (moved) {
        const delta = orientation === 'vertical' ? -dy : dx
        options.onDragDelta(delta)
      }
    }

    const onUp = (ev: PointerEvent) => {
      if (ev.pointerId !== pointerId) return
      cleanup()
      if (moved) {
        options.onDragEnd()
      } else if (ctrl) {
        options.onReset()
      }
    }

    const onCancel = (ev: PointerEvent) => {
      if (ev.pointerId !== pointerId) return
      cleanup()
      if (moved) options.onDragEnd()
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
}
