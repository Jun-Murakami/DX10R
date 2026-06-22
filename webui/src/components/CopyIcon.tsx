// Copy (clipboard) icon for the effect-chain Copy button (ported from Synth-80).
// No fixed fill so SvgIcon's default fill=currentColor lets `color` drive it.

import { SvgIcon, type SvgIconProps } from '@mui/material'

export function CopyIcon(props: SvgIconProps) {
  return (
    <SvgIcon viewBox="0 0 15.93 19.56" {...props}>
      <rect x="3.67" y="6.98" width="8.61" height="1" />
      <rect x="3.67" y="9.44" width="8.61" height="1" />
      <rect x="3.66" y="11.9" width="8.61" height="1" />
      <rect x="3.67" y="14.35" width="8.61" height="1" />
      <path d="M12.27,2.26v-.91h-2.16c-.38-.81-1.2-1.35-2.14-1.35s-1.76.54-2.14,1.35h-2.16v.91c-2.05.17-3.67,1.87-3.67,3.96v9.34c0,2.2,1.79,3.99,3.99,3.99h7.95c2.2,0,3.99-1.79,3.99-3.99V6.22c0-2.09-1.62-3.79-3.67-3.96ZM7.97.93c.69,0,1.24.56,1.24,1.24s-.56,1.24-1.24,1.24-1.24-.56-1.24-1.24.56-1.24,1.24-1.24ZM14.43,15.57c0,1.37-1.12,2.49-2.49,2.49H3.99c-1.37,0-2.49-1.12-2.49-2.49V6.22c0-1.26.95-2.31,2.17-2.47v1.35h8.6v-1.35c1.22.16,2.17,1.21,2.17,2.47v9.34Z" />
    </SvgIcon>
  )
}
