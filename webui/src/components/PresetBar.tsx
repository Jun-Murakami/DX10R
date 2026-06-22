import { Box, IconButton, MenuItem, Select } from '@mui/material'
import { useState } from 'react'

import { FACTORY_PRESETS } from '../factory-presets'
import { useParamStore } from '../store/paramStore'

// Factory preset selector (prev / dropdown / next). Loading a preset writes all
// 18 normalized values to the store and forwards each to C++ (SPVFUI). Phase 3a:
// factory presets only; user save/load to .dx10p files arrives in Phase 3b.
export function PresetBar() {
  const [index, setIndex] = useState(0)
  const setManyFromUI = useParamStore((s) => s.setManyFromUI)

  const load = (i: number) => {
    const n = FACTORY_PRESETS.length
    const clamped = ((i % n) + n) % n
    setIndex(clamped)
    const rec: Record<number, number> = {}
    FACTORY_PRESETS[clamped].values.forEach((v, k) => {
      rec[k] = v
    })
    setManyFromUI(rec)
  }

  return (
    <Box sx={{ display: 'flex', alignItems: 'center', gap: 0.25 }}>
      <IconButton
        size="small"
        onClick={() => load(index - 1)}
        sx={{ color: 'text.secondary', p: 0.25, fontSize: '1rem', lineHeight: 1 }}
      >
        ‹
      </IconButton>
      <Select
        value={index}
        onChange={(e) => load(Number(e.target.value))}
        variant="standard"
        disableUnderline
        sx={{
          minWidth: 160,
          fontSize: '0.78rem',
          color: 'text.primary',
          bgcolor: 'background.paper',
          borderRadius: 1,
          px: 1,
          '& .MuiSelect-select': { py: 0.25 },
        }}
        MenuProps={{ slotProps: { paper: { sx: { maxHeight: 360 } } } }}
      >
        {FACTORY_PRESETS.map((p, i) => (
          <MenuItem key={p.name} value={i} sx={{ fontSize: '0.78rem' }}>
            {p.name}
          </MenuItem>
        ))}
      </Select>
      <IconButton
        size="small"
        onClick={() => load(index + 1)}
        sx={{ color: 'text.secondary', p: 0.25, fontSize: '1rem', lineHeight: 1 }}
      >
        ›
      </IconButton>
    </Box>
  )
}
