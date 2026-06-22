import { Box, Typography } from '@mui/material'
import type { ReactNode } from 'react'

interface Props {
  title: string
  children: ReactNode
}

// Lightweight titled panel (dark paper + accent caption). DX10R has a flat single
// layer of sections, so this is much simpler than Synth80's flow-aware Section.
export function Section({ title, children }: Props) {
  return (
    <Box
      sx={{
        position: 'relative',
        backgroundColor: 'background.paper',
        borderRadius: 1,
        border: '1px solid rgba(255,255,255,0.08)',
        px: 1.5,
        pt: 2.75,
        pb: 1.25,
        display: 'flex',
        flexDirection: 'column',
        gap: 0.75,
      }}
    >
      <Typography
        variant="caption"
        sx={{
          position: 'absolute',
          top: 5,
          left: 10,
          fontSize: 9.5,
          fontWeight: 700,
          letterSpacing: '0.18em',
          textTransform: 'uppercase',
          color: 'primary.main',
          lineHeight: 1,
        }}
      >
        {title}
      </Typography>
      {children}
    </Box>
  )
}
