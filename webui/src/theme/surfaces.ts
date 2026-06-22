// LCD panel surface for the effect rack (ported from Synth-80). A pale blue-grey
// fill + inset shadow gives a recessed liquid-crystal look; the navy "ink"
// (#1a242b) text and knob arcs read against it.

import type { SxProps, Theme } from '@mui/material'

export const lcdPanelSx = {
  background: 'linear-gradient(180deg, #889099 0%, #6e7984 50%, #5a6470 100%)',
  borderRadius: 1,
  border: '1px solid rgba(0,0,0,0.5)',
  // subtle grey drop shadow on all text / SVG for a recessed feel.
  '& .MuiTypography-root': {
    textShadow: '0 1px 1px rgba(0,0,0,0.28)',
  },
  '& svg': {
    filter: 'drop-shadow(0 1px 1px rgba(0,0,0,0.22))',
  },
  boxShadow: 'inset 0 2px 4px rgba(0,0,0,0.55), inset 0 -1px 1px rgba(255,255,255,0.08)',
} as const satisfies SxProps<Theme>
