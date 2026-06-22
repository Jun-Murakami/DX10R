import { Box, CssBaseline, ThemeProvider, Typography } from '@mui/material'

import { ADSRGraph } from './components/ADSRGraph'
import { EffectsRack } from './components/EffectsRack'
import { HorizontalParameter } from './components/HorizontalParameter'
import { PresetBar } from './components/PresetBar'
import { Section } from './components/Section'
import { paramsForSection, SECTIONS } from './dx10r-params'
import { darkTheme } from './theme'

export default function App() {
  return (
    <ThemeProvider theme={darkTheme}>
      <CssBaseline />
      <Box
        sx={{
          minHeight: '100vh',
          bgcolor: 'background.default',
          p: 1.5,
          display: 'flex',
          flexDirection: 'column',
          gap: 1,
        }}
      >
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, px: 0.5 }}>
          <Typography
            sx={{ fontWeight: 700, letterSpacing: 2, color: 'primary.main', fontSize: '1.2rem' }}
          >
            DX10R
          </Typography>
          <Typography variant="caption" sx={{ color: 'text.secondary' }}>
            2-op FM
          </Typography>
          <Box sx={{ flexGrow: 1 }} />
          <PresetBar />
        </Box>

        <Box
          sx={{
            display: 'grid',
            gridTemplateColumns: 'repeat(auto-fill, minmax(280px, 1fr))',
            gap: 1,
            alignItems: 'start',
          }}
        >
          {SECTIONS.map((section) => (
            <Section key={section} title={section}>
              {section === 'Amp Env' && (
                <Box sx={{ mb: 0.5 }}>
                  <ADSRGraph attackIdx={0} decayIdx={1} sustainIdx={2} releaseIdx={3} height={64} />
                </Box>
              )}
              {paramsForSection(section).map((p) => (
                <HorizontalParameter
                  key={p.idx}
                  paramId={p.idx}
                  label={p.label}
                  min={p.min}
                  max={p.max}
                  unit={p.unit}
                  format={p.format}
                />
              ))}
            </Section>
          ))}
        </Box>

        <Section title="Effects">
          <EffectsRack />
        </Section>
      </Box>
    </ThemeProvider>
  )
}
