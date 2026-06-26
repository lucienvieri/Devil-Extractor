# Ad-Block Filter

CSS class + data-attribute ad-block filter.

Drops elements whose class or data-* attributes match known ad,
sidebar, modal, cookie-banner, and navigation patterns. Uses
lexbor's class_attribute() API and iterates all attributes for
data-* matching.

## Pattern matching

Case-insensitive substring matching. No regex, no regex library dependency.
All patterns are lowercase; the class token is lowercased before comparing.

### Patterns matched

| Category   | Patterns |
|------------|----------|
| Ad slots | ad, ads, advert, sponsored, promoted |
| Sidebars | sidebar, side-bar, aside-nav, column, right-col, left-col |
| Modals | modal, overlay, popup, dialog, lightbox, cta |
| Cookie | cookie, consent, gdpr, banner-consent, opt-in |
| Navigation | nav, navbar, navigation, header-nav, footer-nav, breadcrumbs |
| Footer | footer, bottom-bar, page-footer, site-footer |
| Misc | sticky, float-bar, bottom-bar, top-bar, fab, tooltip |
| Analytics | analytics, ga-, gtag-, piwik, matomo, mixpanel, segment, hotjar |
| Social | share-, social-share, follow-us, like-btn, tweet-btn |
| Forms | newsletter, subscribe, signup, contact-form, cta-button |
| Widgets | widget, widget-area, sidebar-widget, related-posts, read-more |
| Video | video-ads, pre-roll, ad-player, ad-container, ad-wrapper |
| Responsive | visible-xs, visible-sm, visible-md, visible-lg, hidden-xs |

### Ad patterns (detailed)

- `advert`, `sponsored`, `promoted`, `native-adv`
- `ad-container`, `ad-wrapper`, `ad-slot`, `ad-space`, `ad-area`
- `ad-placeholder`, `ad-render`, `ad-target`, `ad-unit`
- `ad-banner`, `ad-display`, `ad-frame`, `ad-iframe`, `ad-img`
- `ad-layer`, `ad-message`, `ad-overlay`, `ad-player`, `ad-pod`
- `ad-position`, `ad-rendering`, `ad-result`, `ad-server`
- `ad-slot-`, `ad-tag`, `ad-text`, `ad-track`, `ad-view`

Note: bare "ad" was intentionally removed — too broad (matches "header",
"radar", "cad", etc.).

### Sidebars / columns

- `sidebar`, `side-bar`, `aside-nav`, `right-col`, `left-col`
- `col-`, `span-`, `grid-col`, `aside`
- `widget-area`, `widget-container`, `widget-box`, `widget`
- `related-posts`, `related-content`, `more-from`, `read-more`

Removed: `column`, `secondary`, `third` — too broad: matches nav menu
columns, secondary buttons, tertiary menu items on modern CSS-in-JS sites.

### Modals / popups

- `modal`, `overlay`, `popup`, `dialog`, `lightbox`, `cta`
- `modal-backdrop`, `modal-content`, `modal-dialog`, `modal-header`
- `modal-body`, `modal-footer`, `modal-size`, `modal-open`
- `popup-`, `popup-overlay`, `popup-content`, `popup-window`
- `lightbox-`, `lightbox-overlay`, `lightbox-content`
- `tooltip`, `float`, `fab`

### Cookie / consent

- `cookie`, `consent`, `gdpr`, `banner-consent`, `opt-in`
- `cookie-banner`, `cookie-notice`, `cookie-accept`, `cookie-policy`
- `privacy-policy`, `privacy-notice`

### Navigation

- `header-nav`, `footer-nav`, `breadcrumbs`, `breadcrumb`, `topnav`
- `sidenav`, `side-nav`, `left-nav`, `right-nav`

Only matches nav as a standalone word context, not inside navbar etc.

### Footer

- `footer`, `bottom-bar`, `page-footer`, `site-footer`

### Analytics / tracking

- `analytics`, `ga-`, `gtag`, `piwik`, `matomo`
- `mixpanel`, `segment`, `hotjar`, `telemetry`
- `tracking`, `tracker`, `pixel`

### Social share buttons

- `share-`, `social-share`, `follow-us`, `like-btn`, `tweet-btn`
- `fb-like`, `fb-share`, `twitter-btn`, `twitter-share`
- `linkedin-share`, `pinterest-btn`, `social-button`

### Newsletter / signup / contact forms

- `newsletter`, `subscribe`, `signup`, `contact-form`, `cta-button`
- `sign-up`, `sign_up`, `email-subscribe`, `mailchimp`

### Video ads

- `video-ads`, `pre-roll`, `ad-player`, `ad-container`, `ad-wrapper`

### Responsive visibility classes

- `visible-xs`, `visible-sm`, `visible-md`, `visible-lg`
- `hidden-xs`, `hidden-sm`, `hidden-md`, `hidden-lg`

### Generic junk patterns

- `sticky`, `float-bar`, `top-bar`, `bottom-bar`
- `page-break`, `skip-link`, `sr-only`, `visually-hidden`
- `skip-navigation`, `back-to-top`

## Data attribute patterns

Checks data-* attributes for ad-related markers:

- `data-ad-` — all ad-related data attributes
- `data-analytics` — analytics tracking
- `data-gtm-` — Google Tag Manager
- `data-track`, `data-tracking` — tracking markers
- `data-consent` — consent management
- `data-cookie` — cookie management
- `data-privacy` — privacy settings
- `data-geo` — geolocation

## Implementation

Registered by `adblock_register()` which sets `ctx->pre_filter` to
`adblock_should_prune`.

### Algorithm

1. **Class attribute check** — Split on whitespace, check each token
   against all pattern categories.
2. **Data-* attribute check** — Iterate all attributes, check for
   data-* prefix and match against ad/analytics patterns.

Either check pruning the entire subtree.
