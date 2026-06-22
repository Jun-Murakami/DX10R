// DX10R dark theme — shares the Zero series / Synth80 tonality (dark base,
// cyan primary, amber accent) with Jost (UI) + Red Hat Mono (numeric).
import { createTheme } from '@mui/material/styles'
import '@fontsource/red-hat-mono/400.css'
import '@fontsource/jost/400.css'
import '@fontsource/jost/600.css'
import '@fontsource/jost/700.css'

export const FONT_SANS =
  '"Jost", Roboto, "Helvetica Neue", Arial, "Noto Sans JP", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif'

export const darkTheme = createTheme({
  palette: {
    mode: 'dark',
    primary: {
      main: '#4fc3f7',
      light: '#8bf6ff',
      dark: '#0093c4',
    },
    secondary: {
      main: '#ffab00',
    },
    background: {
      default: '#606F77',
      paper: '#252525',
    },
    text: {
      primary: '#e0e0e0',
      secondary: '#a0a0a0',
    },
  },
  typography: {
    fontFamily: FONT_SANS,
    h6: {
      fontSize: '1.1rem',
      fontWeight: 500,
    },
  },
  shape: {
    borderRadius: 8,
  },
  components: {
    MuiCssBaseline: {
      styleOverrides: {
        '*, *::before, *::after': {
          WebkitUserSelect: 'none',
          userSelect: 'none',
        },
        'input, textarea': {
          WebkitUserSelect: 'text',
          userSelect: 'text',
        },
      },
    },
    MuiButton: {
      styleOverrides: {
        root: {
          textTransform: 'none',
          fontWeight: 500,
        },
      },
    },
    MuiPaper: {
      styleOverrides: {
        root: {
          backgroundImage: 'none',
        },
      },
    },
  },
})
