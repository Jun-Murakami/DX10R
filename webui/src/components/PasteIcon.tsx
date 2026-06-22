// Paste (clipboard) icon for the effect-chain Paste button (ported from Synth-80).
// No fixed fill so SvgIcon's default fill=currentColor lets `color` drive it.

import { SvgIcon, type SvgIconProps } from '@mui/material'

export function PasteIcon(props: SvgIconProps) {
  return (
    <SvgIcon viewBox="0 0 16.94 19.56" {...props}>
      <rect x="9.87" y="6.98" width="3.41" height="1" />
      <rect x="4.67" y="6.98" width="1.07" height="1" />
      <rect x="9.87" y="14.35" width="3.41" height="1" />
      <rect x="4.67" y="14.35" width="1.07" height="1" />
      <polygon points="13.1 11.52 6.41 6.07 6.41 9.14 0 9.14 0 13.77 6.41 13.77 6.41 16.68 13.1 11.52" />
      <path d="M13.27,2.26v-.91h-2.16c-.38-.81-1.2-1.35-2.14-1.35s-1.76.54-2.14,1.35h-2.16v.91c-2.05.17-3.67,1.87-3.67,3.96v2.58h1.5v-2.58c0-1.26.95-2.31,2.17-2.47v1.34h8.6v-1.34c1.22.16,2.17,1.21,2.17,2.47v9.34c0,1.37-1.12,2.49-2.49,2.49h-7.95c-1.37,0-2.49-1.12-2.49-2.49v-1.57h-1.5v1.57c0,2.2,1.79,3.99,3.99,3.99h7.95c2.2,0,3.99-1.79,3.99-3.99V6.22c0-2.09-1.62-3.79-3.67-3.96ZM8.97,3.5c-.73,0-1.33-.59-1.33-1.33s.59-1.33,1.33-1.33,1.33.59,1.33,1.33-.59,1.33-1.33,1.33Z" />
    </SvgIcon>
  )
}
