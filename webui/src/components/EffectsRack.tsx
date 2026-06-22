// DX10R effect rack — faithful port of Synth-80's LCD-style EffectsRack.
//
// The one section that intentionally uses Synth-80's knob + LCD look instead of
// DX10R's horizontal sliders: a pale blue-grey LCD panel with navy "ink", a
// horizontal slot-chain (slot1 → → slot5) of draggable tabs with bypass-dot LEDs
// and a ▼ type menu, an LCD edit area of rotary knobs for the selected slot,
// EFFECT LOCK, and chain Copy/Paste.
//
// Synth-80-specific infra dropped/adapted for DX10R: responsive uiScale → fixed
// sizes; flow-overlay module registration, MIDI-learn, Patch-Volume column, FX
// variant, i18n → removed/English; @mui/icons-material → inline SVG glyphs.

import { DndContext, type DragEndEvent, PointerSensor, useSensor, useSensors } from '@dnd-kit/core'
import { restrictToHorizontalAxis, restrictToParentElement } from '@dnd-kit/modifiers'
import { horizontalListSortingStrategy, SortableContext, useSortable } from '@dnd-kit/sortable'
import { CSS } from '@dnd-kit/utilities'
import { Box, Menu, MenuItem, Tooltip, Typography } from '@mui/material'
import { Fragment, useState } from 'react'
import { useShallow } from 'zustand/react/shallow'

import { EFFECT_TYPE_DEFS, getEffectTypeMeta } from '../dx10r-effects'
import {
  effectChainToJson,
  effectChainToParamWrites,
  parseEffectChain,
} from '../effect-chain-clipboard'
import {
  EFFECT_CHAIN_LOCK_IDX,
  EFFECT_PARAMS_PER_SLOT,
  EFFECTS_SLOT_COUNT,
  effectSlotBypassIdx,
  effectSlotParamIdx,
  effectSlotTypeIdx,
  effectTypeIdxFromValue,
  effectTypeValueFromIdx,
} from '../effect-params'
import { useParamStore } from '../store/paramStore'
import { lcdPanelSx } from '../theme/surfaces'
import { CopyIcon } from './CopyIcon'
import { ChevronDownIcon, LockIcon, LockOpenIcon } from './EffectIcons'
import { EffectKnob } from './EffectKnob'
import { PasteIcon } from './PasteIcon'

const EFFECT_COLUMNS = 8

// LCD monotone palette: navy "ink" on a pale blue-grey panel.
const LCD_INK = '#1a242b'
const LCD_FILL = '#2c3a45'
const LCD_FILL_HOVER = '#3a4855'
const LCD_INK_LIGHT = '#cdd5da'
const LCD_INK_FAINT = 'rgba(26,36,43,0.55)'

const SLOT_ID_PREFIX = 'effect-slot-'
const idxToSlotId = (idx: number): string => `${SLOT_ID_PREFIX}${idx}`
const slotIdToIdx = (id: string | number): number => Number(String(id).slice(SLOT_ID_PREFIX.length))

// =============================================================================
// EffectTab: one slot tab — bypass dot + drag-handle name body + ▼ type menu.
// =============================================================================

interface EffectTabProps {
  slotIdx: number
  active: boolean
  onSelect: () => void
  transitionEnabled: boolean
}

function EffectTab({ slotIdx, active, onSelect, transitionEnabled }: EffectTabProps) {
  const typeParamIdx = effectSlotTypeIdx(slotIdx)
  const bypassParamIdx = effectSlotBypassIdx(slotIdx)
  const value = useParamStore((s) => s.values[typeParamIdx] ?? 0)
  const bypassValue = useParamStore((s) => s.values[bypassParamIdx] ?? 0)
  const setFromUI = useParamStore((s) => s.setFromUI)
  const beginGesture = useParamStore((s) => s.beginGesture)
  const endGesture = useParamStore((s) => s.endGesture)

  const [anchorEl, setAnchorEl] = useState<HTMLElement | null>(null)
  const open = Boolean(anchorEl)

  const typeIdx = effectTypeIdxFromValue(value)
  const typeMeta = getEffectTypeMeta(typeIdx)
  const isOff = typeMeta.id === 0
  const isBypassed = Math.round(bypassValue) >= 1

  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({
    id: idxToSlotId(slotIdx),
  })

  const showAsActive = active && !isDragging

  const handleBypassClick = (e: React.MouseEvent<HTMLElement>) => {
    e.stopPropagation()
    const next = isBypassed ? 0 : 1
    beginGesture(bypassParamIdx)
    setFromUI(bypassParamIdx, next)
    endGesture(bypassParamIdx)
    onSelect()
  }

  const handleDropdownClick = (e: React.MouseEvent<HTMLElement>) => {
    e.stopPropagation()
    setAnchorEl(e.currentTarget)
  }
  const handleClose = () => setAnchorEl(null)
  const handleSelectType = (newIdx: number) => {
    // C++ reapplies the slot's CURRENT 8 values on type-switch, so the UI writes
    // this effect's per-knob defaults (Type first, then each generic param).
    beginGesture(typeParamIdx)
    setFromUI(typeParamIdx, effectTypeValueFromIdx(newIdx))
    endGesture(typeParamIdx)
    const newMeta = getEffectTypeMeta(newIdx)
    for (const knob of newMeta.knobs) {
      if (knob.defaultValue === undefined) continue
      const pIdx = effectSlotParamIdx(slotIdx, knob.paramOffset)
      beginGesture(pIdx)
      setFromUI(pIdx, knob.defaultValue)
      endGesture(pIdx)
    }
    handleClose()
    onSelect()
  }

  const sortableStyle: React.CSSProperties = {
    transform: CSS.Transform.toString(transform),
    transition: transitionEnabled ? transition : 'none',
  }

  return (
    <Box
      ref={setNodeRef}
      style={sortableStyle}
      sx={{
        flex: 1,
        minWidth: 0,
        display: 'flex',
        alignItems: 'stretch',
        height: 22,
        borderRadius: 0.5,
        overflow: 'hidden',
        backgroundColor: showAsActive ? LCD_FILL : 'rgba(0,0,0,0.18)',
        border: '1px solid rgba(0,0,0,0.35)',
        opacity: isDragging ? 0.35 : isBypassed ? (showAsActive ? 0.7 : 0.5) : 1.0,
        zIndex: isDragging ? 2 : 1,
        position: 'relative',
      }}
    >
      {/* Bypass dot (LED: filled = active, empty = bypassed). */}
      <Box
        onClick={handleBypassClick}
        sx={{
          width: 18,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          borderRight: '1px solid rgba(0,0,0,0.35)',
          cursor: 'pointer',
          '&:hover': { backgroundColor: 'rgba(255,255,255,0.08)' },
        }}
      >
        <Box
          sx={{
            width: 8,
            height: 8,
            borderRadius: '50%',
            border: `1px solid ${LCD_INK}`,
            backgroundColor: !isBypassed ? LCD_INK : 'transparent',
          }}
        />
      </Box>

      {/* Tab body = drag handle; click (under the 5px sensor threshold) selects. */}
      <Box
        {...attributes}
        {...listeners}
        onClick={onSelect}
        sx={{
          flex: 1,
          minWidth: 0,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          px: 0.5,
          color: showAsActive ? LCD_INK_LIGHT : isOff ? LCD_INK_FAINT : LCD_INK,
          fontSize: 10,
          fontWeight: 700,
          letterSpacing: '0.04em',
          whiteSpace: 'nowrap',
          textOverflow: 'ellipsis',
          overflow: 'hidden',
          touchAction: 'none',
          cursor: isDragging ? 'grabbing' : 'grab',
          '&:hover': { backgroundColor: showAsActive ? LCD_FILL_HOVER : 'rgba(0,0,0,0.08)' },
          outline: 'none',
        }}
      >
        {typeMeta.name}
      </Box>

      {/* ▼ type dropdown trigger */}
      <Box
        onClick={handleDropdownClick}
        data-testid={`fx-type-trigger-${slotIdx}`}
        sx={{
          width: 18,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          borderLeft: '1px solid rgba(0,0,0,0.35)',
          color: showAsActive ? LCD_INK_LIGHT : LCD_INK,
          cursor: 'pointer',
          '&:hover': { backgroundColor: 'rgba(255,255,255,0.08)' },
        }}
      >
        <ChevronDownIcon sx={{ fontSize: 14 }} />
      </Box>
      <Menu
        open={open}
        anchorEl={anchorEl}
        onClose={handleClose}
        anchorOrigin={{ vertical: 'bottom', horizontal: 'left' }}
        transformOrigin={{ vertical: 'top', horizontal: 'left' }}
        marginThreshold={4}
        slotProps={{
          paper: { sx: { minWidth: 140, maxHeight: 'calc(100vh - 8px)' } },
          list: { sx: { py: 0.25 } },
        }}
      >
        {EFFECT_TYPE_DEFS.map((meta) => (
          <MenuItem
            key={meta.id}
            selected={meta.id === typeIdx}
            onClick={() => handleSelectType(meta.id)}
            sx={{ fontSize: 13, py: 0.4, minHeight: 0, lineHeight: 1.4 }}
          >
            {meta.name}
          </MenuItem>
        ))}
      </Menu>
    </Box>
  )
}

// → arrow between tabs (signal-flow direction).
function TabArrow() {
  return (
    <Box
      sx={{
        flexShrink: 0,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        width: 12,
        color: LCD_INK,
        fontSize: 13,
        fontWeight: 700,
        lineHeight: 1,
        userSelect: 'none',
      }}
    >
      →
    </Box>
  )
}

// =============================================================================
// EffectEditPage: 8-col grid of the active slot's knobs (visibleWhen gated).
// =============================================================================

function EffectEditPage({ slotIdx }: { slotIdx: number }) {
  const typeParamIdx = effectSlotTypeIdx(slotIdx)
  const bypassParamIdx = effectSlotBypassIdx(slotIdx)
  const typeValue = useParamStore((s) => s.values[typeParamIdx] ?? 0)
  const bypassValue = useParamStore((s) => s.values[bypassParamIdx] ?? 0)
  const typeMeta = getEffectTypeMeta(effectTypeIdxFromValue(typeValue))
  const isBypassed = Math.round(bypassValue) >= 1

  // Subscribe to this slot's knob values (for visibleWhen). useShallow keeps the
  // derived object stable so re-renders don't loop.
  const slotValues = useParamStore(
    useShallow((s) => {
      const out: Record<number, number> = {}
      for (const k of typeMeta.knobs) {
        out[k.paramOffset] = s.values[effectSlotParamIdx(slotIdx, k.paramOffset)] ?? 0
      }
      return out
    }),
  )

  const visibleKnobs = typeMeta.knobs.filter((knob) => {
    if (!knob.visibleWhen) return true
    const refMeta = typeMeta.knobs.find((k) => k.paramOffset === knob.visibleWhen?.paramOffset)
    if (!refMeta?.enumOptions) return true
    const n = refMeta.enumOptions.length
    const refValue01 = slotValues[knob.visibleWhen.paramOffset] ?? 0
    const currentIdx = Math.min(n - 1, Math.max(0, Math.round(refValue01 * (n - 1))))
    return currentIdx === knob.visibleWhen.enumIdx
  })

  return (
    <Box
      sx={{
        flex: 1,
        minWidth: 0,
        display: 'grid',
        gridTemplateColumns: `repeat(${EFFECT_COLUMNS}, 1fr)`,
        alignItems: 'stretch',
        justifyItems: 'center',
        px: 1,
        py: 0.5,
        opacity: isBypassed ? 0.45 : 1.0,
        transition: 'opacity 120ms ease',
      }}
    >
      {visibleKnobs.length === 0 ? (
        <Box
          sx={{
            gridColumn: `1 / ${EFFECT_COLUMNS + 1}`,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            color: LCD_INK_FAINT,
            fontSize: 10,
            fontStyle: 'italic',
          }}
        >
          — slot {slotIdx + 1}: {typeMeta.name} —
        </Box>
      ) : (
        visibleKnobs.map((knobMeta) => (
          <EffectKnob
            key={knobMeta.paramOffset}
            paramIdx={effectSlotParamIdx(slotIdx, knobMeta.paramOffset)}
            meta={knobMeta}
          />
        ))
      )}
    </Box>
  )
}

// =============================================================================
// LockButton: Effect Chain Lock toggle (ON → patch load won't overwrite the
// effect chain — relevant once user presets carry effect params).
// =============================================================================

function LockButton() {
  const lockValue = useParamStore((s) => s.values[EFFECT_CHAIN_LOCK_IDX] ?? 0)
  const setFromUI = useParamStore((s) => s.setFromUI)
  const beginGesture = useParamStore((s) => s.beginGesture)
  const endGesture = useParamStore((s) => s.endGesture)
  const locked = Math.round(lockValue) >= 1

  const onClick = () => {
    const next = locked ? 0 : 1
    beginGesture(EFFECT_CHAIN_LOCK_IDX)
    setFromUI(EFFECT_CHAIN_LOCK_IDX, next)
    endGesture(EFFECT_CHAIN_LOCK_IDX)
  }

  return (
    <Tooltip
      title={locked ? 'Effect chain locked (patch load keeps effects)' : 'Effect chain unlocked'}
      placement="top"
      arrow
    >
      <Box
        onClick={onClick}
        sx={{
          display: 'inline-flex',
          alignItems: 'center',
          height: 22,
          gap: 0.25,
          px: 0.5,
          py: 0.1,
          borderRadius: 0.5,
          border: locked ? '1px solid rgba(0,0,0,0.5)' : '1px solid rgba(0,0,0,0.35)',
          backgroundColor: locked ? LCD_FILL : 'rgba(255,255,255,0.05)',
          color: locked ? LCD_INK_LIGHT : LCD_INK,
          fontSize: 9,
          fontWeight: 600,
          letterSpacing: '0.02em',
          lineHeight: 1,
          cursor: 'pointer',
          userSelect: 'none',
          '&:hover': { backgroundColor: locked ? LCD_FILL_HOVER : 'rgba(0,0,0,0.08)' },
        }}
      >
        {locked ? (
          <LockIcon sx={{ fontSize: 13, flexShrink: 0 }} />
        ) : (
          <LockOpenIcon sx={{ fontSize: 13, flexShrink: 0 }} />
        )}
        <Box component="span" sx={{ textAlign: 'left', whiteSpace: 'normal', lineHeight: 1.05 }}>
          EFFECT LOCK
        </Box>
      </Box>
    </Tooltip>
  )
}

// =============================================================================
// EffectChainClipboardButtons: Copy / Paste the whole 5-slot chain as JSON.
// Uses the OS clipboard via navigator.clipboard when available (web / browser);
// the plugin WebView is a null-origin context where that's unavailable, so a
// module-level session buffer is always kept as a fallback.
// =============================================================================

let sessionClipboard: string | null = null

function EffectChainClipboardButtons() {
  const [feedback, setFeedback] = useState<'idle' | 'copied' | 'pasted'>('idle')

  const flash = (state: 'copied' | 'pasted') => {
    setFeedback(state)
    window.setTimeout(() => setFeedback('idle'), 1100)
  }

  const handleCopy = async () => {
    const json = effectChainToJson(useParamStore.getState().values)
    sessionClipboard = json
    try {
      if (navigator?.clipboard) await navigator.clipboard.writeText(json)
    } catch {
      // null-origin plugin WebView: session buffer already holds it.
    }
    flash('copied')
  }

  const handlePaste = async () => {
    let text: string | null = null
    try {
      if (navigator?.clipboard) text = await navigator.clipboard.readText()
    } catch {
      // fall through to session buffer
    }
    if (!text) text = sessionClipboard
    if (!text) return
    const data = parseEffectChain(text)
    if (!data) {
      console.warn('[EffectsRack] clipboard does not contain a valid effect chain')
      return
    }
    const store = useParamStore.getState()
    for (const [idx, v] of effectChainToParamWrites(data)) {
      store.beginGesture(idx)
      store.setFromUI(idx, v)
      store.endGesture(idx)
    }
    flash('pasted')
  }

  const itemSx = {
    display: 'inline-flex',
    alignItems: 'center',
    gap: 0.4,
    color: LCD_INK,
    fontSize: 10,
    fontWeight: 700,
    letterSpacing: '0.04em',
    lineHeight: 1,
    cursor: 'pointer',
    userSelect: 'none',
    opacity: 0.7,
    transition: 'opacity 120ms ease',
    '&:hover': { opacity: 1 },
  } as const

  return (
    <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
      <Tooltip title="Copy effect chain" arrow>
        <Box onClick={handleCopy} sx={itemSx}>
          <CopyIcon sx={{ fontSize: 12 }} />
          {feedback === 'copied' ? 'Copied' : 'Copy'}
        </Box>
      </Tooltip>
      <Tooltip title="Paste effect chain" arrow>
        <Box onClick={handlePaste} sx={itemSx}>
          <PasteIcon sx={{ fontSize: 12 }} />
          {feedback === 'pasted' ? 'Pasted' : 'Paste'}
        </Box>
      </Tooltip>
    </Box>
  )
}

// =============================================================================
// EffectsRack: wrapper (LCD panel + vertical EFFECT CHAIN label + tabs/edit +
// EFFECT LOCK column).
// =============================================================================

/** Reorder slots: snapshot the 10 IParam values per slot, rewrite in new order. */
function reorderEffectSlots(src: number, target: number): void {
  if (src === target) return
  const store = useParamStore.getState()
  const values = store.values
  type Snap = { type: number; params: number[]; bypass: number }
  const snaps: Snap[] = []
  for (let i = 0; i < EFFECTS_SLOT_COUNT; i++) {
    const params: number[] = []
    for (let p = 0; p < EFFECT_PARAMS_PER_SLOT; p++) {
      params.push(values[effectSlotParamIdx(i, p)] ?? 0)
    }
    snaps.push({
      type: values[effectSlotTypeIdx(i)] ?? 0,
      params,
      bypass: values[effectSlotBypassIdx(i)] ?? 0,
    })
  }
  const order = Array.from({ length: EFFECTS_SLOT_COUNT }, (_, i) => i)
  const [removed] = order.splice(src, 1)
  order.splice(target, 0, removed)
  for (let newPos = 0; newPos < EFFECTS_SLOT_COUNT; newPos++) {
    const oldPos = order[newPos]
    if (oldPos === newPos) continue
    const snap = snaps[oldPos]
    const writeOne = (idx: number, v: number) => {
      store.beginGesture(idx)
      store.setFromUI(idx, v)
      store.endGesture(idx)
    }
    writeOne(effectSlotTypeIdx(newPos), snap.type)
    for (let p = 0; p < EFFECT_PARAMS_PER_SLOT; p++) {
      writeOne(effectSlotParamIdx(newPos, p), snap.params[p])
    }
    writeOne(effectSlotBypassIdx(newPos), snap.bypass)
  }
}

function shiftActiveSlot(active: number, src: number, target: number): number {
  if (active === src) return target
  if (src < active && active <= target) return active - 1
  if (target <= active && active < src) return active + 1
  return active
}

const SORTABLE_ITEM_IDS = Array.from({ length: EFFECTS_SLOT_COUNT }, (_, i) => idxToSlotId(i))

export function EffectsRack() {
  const [activeSlot, setActiveSlot] = useState(0)
  const [transitionEnabled, setTransitionEnabled] = useState(true)
  const sensors = useSensors(useSensor(PointerSensor, { activationConstraint: { distance: 5 } }))

  const handleDragEnd = (event: DragEndEvent) => {
    const { active, over } = event
    if (!over || active.id === over.id) return
    const src = slotIdToIdx(active.id)
    const target = slotIdToIdx(over.id)
    if (Number.isNaN(src) || Number.isNaN(target)) return
    setTransitionEnabled(false)
    reorderEffectSlots(src, target)
    setActiveSlot((a) => shiftActiveSlot(a, src, target))
    requestAnimationFrame(() => {
      requestAnimationFrame(() => setTransitionEnabled(true))
    })
  }

  return (
    <Box
      sx={{
        ...lcdPanelSx,
        display: 'flex',
        alignItems: 'stretch',
        minHeight: 132,
        p: 0.5,
      }}
    >
      {/* Left: vertical "EFFECT CHAIN" label + divider. */}
      <Box
        sx={{
          width: 18,
          flexShrink: 0,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          borderRight: '1px solid rgba(0,0,0,0.25)',
        }}
      >
        <Typography
          sx={{
            fontSize: 9.5,
            fontWeight: 700,
            letterSpacing: '0.18em',
            color: LCD_INK,
            writingMode: 'vertical-rl',
            transform: 'rotate(180deg)',
            whiteSpace: 'nowrap',
          }}
        >
          EFFECT CHAIN
        </Typography>
      </Box>

      {/* Center: tab strip + edit page; Copy/Paste pinned bottom-right. */}
      <Box
        sx={{
          flex: 1,
          minWidth: 0,
          display: 'flex',
          flexDirection: 'column',
          gap: 0.5,
          px: 0.75,
          py: 0.75,
          position: 'relative',
        }}
      >
        <DndContext
          sensors={sensors}
          onDragEnd={handleDragEnd}
          modifiers={[restrictToHorizontalAxis, restrictToParentElement]}
        >
          <SortableContext items={SORTABLE_ITEM_IDS} strategy={horizontalListSortingStrategy}>
            <Box sx={{ display: 'flex', alignItems: 'stretch' }}>
              {Array.from({ length: EFFECTS_SLOT_COUNT }, (_, i) => (
                <Fragment key={idxToSlotId(i)}>
                  {i > 0 && <TabArrow />}
                  <EffectTab
                    slotIdx={i}
                    active={activeSlot === i}
                    onSelect={() => setActiveSlot(i)}
                    transitionEnabled={transitionEnabled}
                  />
                </Fragment>
              ))}
            </Box>
          </SortableContext>
        </DndContext>
        <Box sx={{ flex: 1, minHeight: 0, display: 'flex' }}>
          <EffectEditPage slotIdx={activeSlot} />
        </Box>
        <Box sx={{ position: 'absolute', right: 4, bottom: 2, zIndex: 2 }}>
          <EffectChainClipboardButtons />
        </Box>
      </Box>

      {/* Right: EFFECT LOCK column (Synth-80's Patch-Volume column slot; DX10R's
          master volume lives in the Master section, so this holds only LOCK). */}
      <Box
        sx={{
          width: 104,
          flexShrink: 0,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          alignSelf: 'stretch',
          borderLeft: '1px dashed rgba(0,0,0,0.25)',
          px: '4px',
        }}
      >
        <LockButton />
      </Box>
    </Box>
  )
}
