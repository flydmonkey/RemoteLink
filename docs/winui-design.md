# RemoteLink WinUI design

This is the visual contract for all browser UI in RemoteLink. It adapts
Windows 11 Fluent and WinUI conventions to HTML/CSS without pretending that a
browser surface can use the native Windows compositor.

## Principles

- Familiar, calm, coherent, and operationally dense.
- Use `Segoe UI Variable`, falling back to `Segoe UI` and the platform CJK UI
  font. Typography communicates hierarchy; it is not decoration.
- Long-lived page backgrounds use an opaque Mica-like neutral tint. Acrylic is
  reserved for transient surfaces such as login and paste dialogs.
- Do not stack adjacent translucent panes. Content layers use low-opacity solid
  fills over the base material.
- Use blue only for selection, focus, connectivity, and primary actions. Error
  and warning colors are semantic, not decorative.

## Tokens

| Role | Light value | Dark value |
| --- | --- | --- |
| Mica base | `#F3F3F3` | `#202020` |
| Content layer | `rgba(0,0,0,0.04)` | `rgba(255,255,255,0.04)` |
| Control fill | `rgba(255,255,255,0.70)` | `rgba(255,255,255,0.06)` |
| Subtle stroke | `rgba(0,0,0,0.06)` | `rgba(255,255,255,0.08)` |
| Strong stroke | `rgba(0,0,0,0.16)` | `rgba(255,255,255,0.16)` |
| Primary text | `#1A1A1A` | `#FFFFFF` |
| Secondary text | `rgba(0,0,0,0.62)` | `rgba(255,255,255,0.60)` |
| Accent | `#005FB8` | `#60CDFF` |
| Accent text | `#FFFFFF` | `#003447` |
| Error | `#C42B1C` | `#FF99A4` |

Controls use a 4 px radius. Top-level content layers, cards, flyouts, and
dialogs use an 8 px radius. Layout spacing follows an 8 px base rhythm, with
12 px between a label and its control where additional separation is needed.

## Structure

- The 56 px title bar contains identity, top-level destinations, connection
  state, and theme selection.
- Session commands use a compact vertical CommandBar on the left of the remote
  canvas. Commands are grouped, use accessible icon buttons with tooltips, and
  the bar can collapse to a single edge handle.
- Remote video is the dominant content surface. Its layout reserves space for
  the CommandBar so controls do not obscure remote pixels.
- Management uses an adaptive left navigation/status pane and one contiguous
  content layer. Target rows are list items, not independent dashboard tiles.
- Persistent events are rendered as a list. A badge is reserved for a compact
  count or non-blocking alert, never as a substitute for status text.

## Interaction

- Every interactive element has hover, pressed, disabled, and keyboard-focus
  states. Primary actions use the accent fill; ordinary commands use neutral
  control fill.
- Blocking actions use a smoke backdrop and an 8 px dialog. Destructive actions
  require confirmation and retain their explicit verb.
- At narrow widths, the navigation/status pane moves above content and list
  rows collapse without hiding essential status or actions.
- Theme selection cycles through system, light, and dark and may be persisted
  because it is not sensitive. System mode follows `prefers-color-scheme`.
- Forced-colors mode discards simulated materials and uses browser system
  colors for surfaces, controls, selection, and focus.

## Responsive states

- Small (`<640px`): single-column content, compact title bar, two-column metric
  grid, and collapsed target/event rows.
- Medium (`641–1007px`): status navigation moves above content and secondary
  event metadata reflows below the main row.
- Large (`>=1008px`): stable 220 px navigation/status pane and full-density
  target and event list columns.

## References

- https://learn.microsoft.com/windows/apps/design/signature-experiences/materials
- https://learn.microsoft.com/windows/apps/design/signature-experiences/geometry
- https://learn.microsoft.com/windows/apps/design/signature-experiences/typography
- https://learn.microsoft.com/windows/apps/design/style/spacing
- https://learn.microsoft.com/windows/apps/design/controls/navigationview
